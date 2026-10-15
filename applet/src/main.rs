use cosmic::app::Core;
use cosmic::iced::futures::{SinkExt, StreamExt};
use cosmic::iced::{Limits, Subscription, window::Id};
use cosmic::surface::action::{app_popup, destroy_popup};
use cosmic::{Element, Task, widget};
use std::path::PathBuf;
use std::time::Duration;
use zbus::{Connection, MatchRule, MessageStream, Proxy, message::Type, proxy::MethodFlags};

const BUS: &str = "org.muness.CalendarReminderOverlay.Core";
const PATH: &str = "/org/muness/CalendarReminderOverlay/Core";
const IFACE: &str = "org.muness.CalendarReminderOverlay.Core1";

#[derive(Clone, Debug)]
enum Message {
    TogglePopup,
    Closed(Id),
    State(Result<bool, String>),
    Toggle(bool),
    Changed(Result<bool, String>),
    OpenSettings,
    EditorOpened(Result<(), String>),
    ApplySettings,
    SettingsApplied(Result<(), String>),
    Refresh,
}

struct Reminders {
    core: Core,
    popup: Option<Id>,
    enabled: Option<bool>,
    busy: bool,
    error: Option<String>,
    editing: bool,
    notice: Option<&'static str>,
}

async fn presentation(set: Option<bool>) -> Result<bool, String> {
    let request = async {
        let connection = Connection::session().await.map_err(|e| e.to_string())?;
        let proxy = Proxy::new(&connection, BUS, PATH, IFACE)
            .await
            .map_err(|e| e.to_string())?;
        if let Some(enabled) = set {
            let accepted: Option<bool> = proxy
                .call_with_flags(
                    "SetPresentation",
                    MethodFlags::NoAutoStart.into(),
                    &(enabled,),
                )
                .await
                .map_err(|e| e.to_string())?;
            if accepted != Some(true) {
                return Err("Could not save. Try again.".into());
            }
        }
        let state: Option<bool> = proxy
            .call_with_flags("GetPresentation", MethodFlags::NoAutoStart.into(), &())
            .await
            .map_err(|e| e.to_string())?;
        state.ok_or_else(|| "Could not read reminder status.".into())
    };
    tokio::time::timeout(Duration::from_secs(5), request).await
        .map_err(|_| "Service not responding. Try again.".to_string())?
        .map_err(|error: String| {
            if error.contains("ServiceUnknown") || error.contains("NameHasNoOwner") {
                "Service stopped. Run: systemctl --user start calendar-reminder-overlay.service".into()
            } else { error }
        })
}

fn installed_command(name: &str) -> Result<PathBuf, String> {
    let home = std::env::var_os("HOME").ok_or("Your home directory is unavailable.")?;
    let path = PathBuf::from(home).join(".local/bin").join(name);
    if path.is_file() {
        Ok(path)
    } else {
        Err(format!(
            "{name} is missing. Rerun ./install.sh --with-applet from the checkout."
        ))
    }
}

async fn settings_command(apply: bool) -> Result<(), String> {
    let path = installed_command(if apply {
        "reminderctl"
    } else {
        "reminder-settings"
    })?;
    let mut command = tokio::process::Command::new(path);
    if apply {
        command.arg("reload");
    }
    command.kill_on_drop(true);
    let result = tokio::time::timeout(Duration::from_secs(10), command.output())
        .await
        .map_err(|_| "Settings request timed out. Try again.".to_string())?
        .map_err(|e| format!("Could not open settings: {e}"))?;
    if result.status.success() {
        Ok(())
    } else {
        let detail = String::from_utf8_lossy(&result.stderr).trim().to_string();
        Err(if detail.is_empty() {
            "Settings failed. Try again.".into()
        } else {
            detail
        })
    }
}

impl cosmic::Application for Reminders {
    type Executor = cosmic::executor::Default;
    type Flags = ();
    type Message = Message;
    const APP_ID: &'static str = "com.muness.CalendarReminderControl";

    fn core(&self) -> &Core {
        &self.core
    }
    fn core_mut(&mut self) -> &mut Core {
        &mut self.core
    }
    fn init(core: Core, _: ()) -> (Self, Task<cosmic::Action<Message>>) {
        (
            Self {
                core,
                popup: None,
                enabled: None,
                busy: false,
                error: None,
                editing: false,
                notice: None,
            },
            Task::none(),
        )
    }
    fn on_close_requested(&self, id: Id) -> Option<Message> {
        Some(Message::Closed(id))
    }

    fn update(&mut self, message: Message) -> Task<cosmic::Action<Message>> {
        match message {
            Message::TogglePopup => {
                if let Some(id) = self.popup.take() {
                    return cosmic::surface::surface_task(destroy_popup(id));
                }
                let popup = cosmic::surface::surface_task(app_popup(
                    |_| Default::default(),
                    |app: &mut Self| {
                        let id = Id::unique();
                        app.popup = Some(id);
                        let mut settings = app.core.applet.get_popup_settings(
                            app.core.main_window_id().unwrap(),
                            id,
                            None,
                            None,
                            None,
                        );
                        settings.positioner.size_limits = Limits::NONE
                            .min_width(320.)
                            .max_width(380.)
                            .max_height(600.);
                        settings
                    },
                    None,
                ));
                return Task::batch([
                    popup,
                    Task::perform(presentation(None), |v| {
                        cosmic::Action::App(Message::State(v))
                    }),
                ]);
            }
            Message::Closed(id) => {
                if self.popup == Some(id) {
                    self.popup = None;
                }
            }
            Message::State(result) => {
                // An in-flight write owns the visible state until its confirmed response.
                if !self.busy {
                    match result {
                        Ok(enabled) => {
                            if self.enabled.is_none() {
                                self.error = None;
                            }
                            self.enabled = Some(enabled);
                        }
                        Err(error) => {
                            self.enabled = None;
                            self.error = Some(error);
                        }
                    }
                }
            }
            Message::Toggle(enabled) => {
                if self.busy || self.enabled.is_none() {
                    return Task::none();
                }
                self.busy = true;
                self.error = None;
                self.notice = None;
                return Task::perform(presentation(Some(enabled)), |v| {
                    cosmic::Action::App(Message::Changed(v))
                });
            }
            Message::Changed(result) => {
                self.busy = false;
                match result {
                    Ok(enabled) => {
                        self.enabled = Some(enabled);
                        self.error = None;
                    }
                    Err(error) => {
                        self.error = Some(error);
                    }
                }
            }
            Message::Refresh => {
                self.error = None;
                return Task::perform(presentation(None), |v| {
                    cosmic::Action::App(Message::State(v))
                });
            }
            Message::OpenSettings => {
                if self.busy {
                    return Task::none();
                }
                self.busy = true;
                self.error = None;
                self.notice = None;
                return Task::perform(settings_command(false), |v| {
                    cosmic::Action::App(Message::EditorOpened(v))
                });
            }
            Message::EditorOpened(result) => {
                self.busy = false;
                match result {
                    Ok(()) => {
                        self.editing = true;
                        self.notice =
                            Some("Save, then click Apply settings.");
                    }
                    Err(error) => self.error = Some(error),
                }
            }
            Message::ApplySettings => {
                if self.busy {
                    return Task::none();
                }
                self.busy = true;
                self.error = None;
                self.notice = None;
                return Task::perform(settings_command(true), |v| {
                    cosmic::Action::App(Message::SettingsApplied(v))
                });
            }
            Message::SettingsApplied(result) => {
                self.busy = false;
                match result {
                    Ok(()) => {
                        self.editing = false;
                        self.notice = Some("Settings applied.");
                    }
                    Err(error) => self.error = Some(error),
                }
            }
        }
        Task::none()
    }

    fn view(&self) -> Element<'_, Message> {
        let icon = match self.enabled {
            Some(true) => "alarm-symbolic",
            Some(false) => "notifications-disabled-symbolic",
            None => "dialog-warning-symbolic",
        };
        self.core
            .applet
            .icon_button(icon)
            .on_press(Message::TogglePopup)
            .into()
    }

    fn view_window(&self, id: Id) -> Element<'_, Message> {
        if self.popup != Some(id) {
            return widget::text("").into();
        }
        let spacing = cosmic::theme::active().cosmic().spacing;
        let label = match self.enabled {
            Some(true) => "Reminders on",
            Some(false) => "Reminders off",
            None => "Reminders unavailable",
        };
        let mut toggle = widget::toggler(self.enabled.unwrap_or(false));
        if self.enabled.is_some() && !self.busy {
            toggle = toggle.on_toggle(Message::Toggle);
        }
        let status = widget::settings::item(label, toggle);
        let mut content = cosmic::iced::widget::Column::new()
            .spacing(spacing.space_s)
            .padding(spacing.space_s)
            .push(widget::text::heading("Calendar reminders"))
            .push(status);
        if self.busy {
            content = content.push(widget::text::body("Working…"));
        }
        if let Some(error) = &self.error {
            content = content.push(widget::text::body(error));
            if !self.busy {
                content =
                    content.push(widget::button::standard("Retry").on_press(Message::Refresh));
            }
        }
        let mut settings = widget::button::standard("Open settings");
        if !self.busy {
            settings = settings.on_press(Message::OpenSettings);
        }
        content = content.push(settings);
        if self.editing {
            let mut apply = widget::button::suggested("Apply settings");
            if !self.busy {
                apply = apply.on_press(Message::ApplySettings);
            }
            content = content.push(apply);
        }
        if let Some(notice) = self.notice {
            content = content.push(widget::text::body(notice));
        }
        self.core.applet.popup_container(content).into()
    }
    fn style(&self) -> Option<cosmic::iced::theme::Style> {
        Some(cosmic::applet::style())
    }

    fn subscription(&self) -> Subscription<Message> {
        Subscription::run(|| {
            cosmic::iced::stream::channel(
                8,
                |mut output: cosmic::iced::futures::channel::mpsc::Sender<Message>| async move {
                    loop {
                        let connection = match Connection::session().await {
                            Ok(c) => c,
                            Err(error) => {
                                let _ = output.send(Message::State(Err(error.to_string()))).await;
                                tokio::time::sleep(Duration::from_secs(5)).await;
                                continue;
                            }
                        };
                        let changes = MatchRule::builder()
                            .msg_type(Type::Signal)
                            .interface(IFACE)
                            .unwrap()
                            .path(PATH)
                            .unwrap()
                            .build();
                        let owners = MatchRule::builder()
                            .msg_type(Type::Signal)
                            .interface("org.freedesktop.DBus")
                            .unwrap()
                            .member("NameOwnerChanged")
                            .unwrap()
                            .arg(0, BUS)
                            .unwrap()
                            .build();
                        let streams = (
                            MessageStream::for_match_rule(changes, &connection, Some(8)).await,
                            MessageStream::for_match_rule(owners, &connection, Some(8)).await,
                        );
                        match streams {
                            (Ok(changes), Ok(owners)) => {
                                let _ = output.send(Message::State(presentation(None).await)).await;
                                let mut events =
                                    cosmic::iced::futures::stream::select(changes, owners);
                                while let Some(event) = events.next().await {
                                    if event.is_err() {
                                        break;
                                    }
                                    let _ =
                                        output.send(Message::State(presentation(None).await)).await;
                                }
                            }
                            _ => {
                                let _ = output
                                    .send(Message::State(Err(
                                        "Connection failed. Reconnecting…"
                                            .into(),
                                    )))
                                    .await;
                            }
                        }
                        let _ = output
                            .send(Message::State(Err(
                                "Disconnected. Reconnecting…".into(),
                            )))
                            .await;
                        tokio::time::sleep(Duration::from_secs(5)).await;
                    }
                },
            )
        })
    }
}

fn main() -> cosmic::iced::Result {
    cosmic::applet::run::<Reminders>(())
}

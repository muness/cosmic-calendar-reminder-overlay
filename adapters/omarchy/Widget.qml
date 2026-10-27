import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui

Panel {
  id: root
  moduleName: "muness.calendar-reminders"
  manageIpc: false
  implicitWidth: trigger.implicitWidth
  implicitHeight: trigger.implicitHeight

  readonly property string adapter: decodeURIComponent(Qt.resolvedUrl("adapter.py").toString().replace(/^file:\/\//, ""))
  property var state: ({ available: false, enabled: null, items: [], active_count: 0 })
  property bool loaded: false
  property string actionError: ""
  property string pendingAction: ""
  property date now: new Date()
  readonly property bool available: loaded && state.available === true
  readonly property bool busy: actionProcess.running
  readonly property var reminders: state.items || []
  readonly property string statusText: !loaded ? "Connecting to reminders…"
    : !available ? "Reminder service unavailable"
    : state.enabled ? "Reminders on" : "Reminders off"
  readonly property string calendarText: !available ? ""
    : !state.eds_enabled ? "Calendar integration is off. Open settings to connect calendars."
    : state.registry_state !== "ready" ? "Connecting to calendar accounts…"
    : state.selected_calendars === 0 ? "No calendars selected. Select calendars in Evolution."
    : state.active_scans > 0 ? "Refreshing calendars…"
    : state.calendar_problems > 0 ? "Some calendars need attention. Open Calendar to check sync."
    : state.selected_calendars + (state.selected_calendars === 1 ? " calendar connected" : " calendars connected")

  function eventTime(item) {
    if (item.state === "active") return "Now"
    if (!item.event_start) return item.state === "snoozed" ? "Snoozed" : "Scheduled"
    var date = new Date(item.event_start * 1000)
    return Qt.formatDate(date, "yyyy-MM-dd") === Qt.formatDate(root.now, "yyyy-MM-dd")
      ? Qt.formatTime(date, "HH:mm") : Qt.formatDateTime(date, "ddd HH:mm")
  }
  function runAction(name) {
    if (busy) return
    actionError = ""
    pendingAction = name
    actionProcess.command = ["/usr/bin/python3", adapter, name]
    actionProcess.running = true
  }

  Timer { interval: 30000; running: true; repeat: true; onTriggered: root.now = new Date() }
  Timer { id: reconnect; interval: 3000; onTriggered: watcher.running = true }
  Process {
    id: watcher
    command: ["/usr/bin/python3", root.adapter, "watch"]
    running: true
    stdout: SplitParser {
      onRead: function(line) {
        try { root.state = JSON.parse(line); root.loaded = true }
        catch (error) { root.actionError = "Could not read reminder status." }
      }
    }
    onExited: {
      root.loaded = true
      root.state = { available: false, enabled: null, items: [], active_count: 0 }
      reconnect.restart()
    }
  }
  Process {
    id: actionProcess
    stdout: StdioCollector {
      onStreamFinished: {
        try {
          var result = JSON.parse(text)
          if (!result.ok) root.actionError = result.error || "The action could not be completed."
        } catch (error) { root.actionError = "The action could not be completed." }
      }
    }
  }

  WidgetButton {
    id: trigger
    anchors.fill: parent
    bar: root.bar
    horizontalMargin: 8.5
    text: !root.available ? "󰂜" : !root.state.enabled ? "󰂛"
      : root.state.active_count > 0 ? "󰂚 " + root.state.active_count
      : root.reminders.length > 0 ? "󰂚 " + root.eventTime(root.reminders[0]) : "󰂚"
    tooltipText: root.available && root.state.enabled && root.reminders.length > 0
      ? root.reminders[0].title : root.available && root.state.enabled ? "No upcoming events or reminders" : root.statusText
    onPressed: root.toggle()
  }

  KeyboardPanel {
    id: popup
    anchorItem: trigger
    owner: root
    bar: root.bar
    open: root.opened
    centerOnBar: false
    contentWidth: fittedContentWidth(Style.space(420))
    contentHeight: fittedContentHeight(content.implicitHeight)
    focusTarget: toggleButton.visible ? toggleButton : startButton

    Flickable {
      anchors.fill: parent
      contentWidth: width
      contentHeight: content.implicitHeight
      clip: true
      boundsBehavior: Flickable.StopAtBounds
      Keys.onEscapePressed: root.close()
      Column {
        id: content
        width: parent.width
        spacing: Style.space(14)

        Text {
          text: "Calendar reminders"
          textFormat: Text.PlainText
          color: ThemeColor.foreground
          font.family: Style.font.family
          font.pixelSize: Style.font.heading
        }
        Row {
          width: parent.width
          spacing: Style.space(10)
          Text {
            width: parent.width - toggleButton.width - parent.spacing
            anchors.verticalCenter: parent.verticalCenter
            text: root.statusText
            textFormat: Text.PlainText
            color: ThemeColor.foreground
            font.family: Style.font.family
            font.pixelSize: Style.font.body
            wrapMode: Text.WordWrap
          }
          Button {
            id: toggleButton
            visible: root.available
            text: root.state.enabled ? "Turn off" : "Turn on"
            focusable: true
            enabled: !root.busy
            onClicked: root.runAction(root.state.enabled ? "disable" : "enable")
          }
        }
        Text {
          width: parent.width
          visible: root.available && (!root.state.eds_enabled || root.state.registry_state !== "ready" || root.state.selected_calendars === 0 || root.state.calendar_problems > 0 || !root.state.enabled)
          text: root.calendarText + (!root.state.enabled ? "\nCalendar sync continues; overlays and sound are paused." : "")
          textFormat: Text.PlainText
          color: ThemeColor.foreground
          font.family: Style.font.family
          font.pixelSize: Style.font.body
          wrapMode: Text.WordWrap
        }
        Text {
          width: parent.width
          visible: root.actionError !== "" || (root.loaded && !root.available)
          text: root.actionError || "Start the reminder service to reconnect. Your settings are saved."
          textFormat: Text.PlainText
          color: ThemeColor.urgent
          font.family: Style.font.family
          font.pixelSize: Style.font.body
          wrapMode: Text.WordWrap
        }
        Button {
          id: startButton
          visible: root.loaded && !root.available
          text: root.busy ? "Starting…" : "Start reminders"
          focusable: true
          enabled: !root.busy
          onClicked: root.runAction("start")
        }
        PanelSeparator { width: parent.width; visible: root.available }
        Text {
          visible: root.available
          text: root.state.active_count > 0 ? "Active and upcoming" : "Upcoming events and reminders"
          color: ThemeColor.foreground
          font.family: Style.font.family
          font.pixelSize: Style.font.body
          font.bold: true
        }
        Repeater {
          model: root.available ? root.reminders : []
          delegate: Row {
            required property var modelData
            width: content.width
            spacing: Style.space(12)
            Text {
              width: Style.space(88)
              text: root.eventTime(modelData)
              textFormat: Text.PlainText
              color: ThemeColor.accent
              font.family: Style.font.family
              font.pixelSize: Style.font.body
              wrapMode: Text.WordWrap
            }
            Text {
              width: parent.width - Style.space(100)
              text: modelData.title
              textFormat: Text.PlainText
              color: ThemeColor.foreground
              font.family: Style.font.family
              font.pixelSize: Style.font.body
              wrapMode: Text.Wrap
              maximumLineCount: 2
              elide: Text.ElideRight
            }
          }
        }
        Text {
          visible: root.available && root.reminders.length === 0
          width: parent.width
          text: "No upcoming events or reminders."
          textFormat: Text.PlainText
          color: ThemeColor.foreground
          font.family: Style.font.family
          font.pixelSize: Style.font.body
          wrapMode: Text.WordWrap
        }
        Flow {
          width: parent.width
          spacing: Style.space(8)
          Button { text: "Refresh"; focusable: true; enabled: root.available && !root.busy; onClicked: root.runAction("refresh") }
          Button { text: "Open calendar"; focusable: true; enabled: !root.busy; onClicked: root.runAction("calendar") }
          Button { text: "Settings"; focusable: true; enabled: !root.busy; onClicked: root.runAction("settings") }
          Button { text: "Apply settings"; focusable: true; enabled: root.available && !root.busy; onClicked: root.runAction("reload") }
          Button { text: "Preview overlay"; focusable: true; enabled: !root.busy; onClicked: { root.runAction("preview"); root.close() } }
        }
        Item {
          width: parent.width
          height: calendarFooter.implicitHeight
          visible: root.available
          Text {
            id: calendarFooter
            width: parent.width
            wrapMode: Text.WordWrap
            text: root.calendarText
            textFormat: Text.PlainText
            color: ThemeColor.foreground
            font.family: Style.font.family
            font.pixelSize: Style.font.bodySmall
          }
          HoverHandler { id: calendarHover }
          PanelToolTip {
            visible: calendarHover.hovered && (root.state.calendar_names || []).length > 0
            text: (root.state.calendar_names || []).join("\n")
          }
        }
      }
    }
  }
}

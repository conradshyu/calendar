# WinCalendar

A native Windows desktop calendar written in C++ against the Win32 API only:

- **User32 / GDI** for the windows and the painted calendar views
- **Windows Ribbon Framework** (`UIRibbon`) for the menu
- **Common Controls** (PropertySheet, ListView, MonthCal, DateTimePicker)
- **WinHTTP** for calendar sync
- **Simple MAPI** for e-mail
- **Windows ICU** (`icu.dll`) for IANA ↔ Windows time-zone mapping
- **Shell** for the tray icon and notifications

It uses no third-party libraries and needs no downloads beyond Visual Studio and the Windows SDK.

## Build

Requirements: Visual Studio 2026 (v18) or 2022 with the **Desktop development with C++** workload. The Windows 10/11 SDK is included in that workload.

* Open `Calendar.sln` and build **Release | x64**, or
* run `build.bat` (uses `vswhere` to find MSBuild; pass `Debug` to build the debug configuration).

The output is `bin\Release\WinCalendar.exe`. The ribbon markup in `src\ribbon.xml` is compiled automatically by `uicc.exe` from the Windows SDK during the build. To regenerate it manually, run `tools\make_ribbon.ps1`. The ribbon icons are produced by `tools\make_icons.ps1`.

## Features and where to find them

| Feature | Ribbon | Dialog (tabs) |
|---|---|---|
| Create, edit and delete events (title, location, dates, duration) | Home › New Event / Open / Delete; double-click or right-click a view | **Event**: General · Recurrence · Time Zone · Attendees · Online Meeting · Reminders |
| Recurring events: daily, weekly on chosen days, monthly by date or "first/second/…/last Monday", yearly; end after N times or on a date; edit one occurrence or the whole series | Home › New Event | Event › Recurrence |
| Day, Week, Month, Year (busy heat map) and Agenda views | Home › Arrange; keys Ctrl+1…5 | – |
| Time zones: follow the Windows zone automatically, use a fixed display zone, lock an event to its own zone (e.g. a flight) or make it "floating", show a secondary zone column | Tools › Time Zones / Use My Location | **Options**: Time Zones; Event › Time Zone |
| Shared calendars for a project, family or client, with view/edit permission per member | Organize › New Calendar / Share Calendar / Open Shared | **Organize Calendars**: Calendars · Sharing · Categories · Accounts |
| Invitations and RSVP tracking: send iCalendar invitations, track accept/decline/tentative/proposed times, answer received invitations (which adds them to your calendar) | Meetings › Send Invitations / RSVP Tracker / Invitations / Accept / Decline / Propose New Time / Open .ics Message | **Meetings**: RSVP Tracking · Invitations |
| Video conferencing: one-click Teams, Zoom, Google Meet, Webex or Jitsi links in the event and in the invitation | Meetings › Teams / Zoom / Google Meet / Jitsi, Join Online | Event › Online Meeting; **Options**: Online Meetings |
| Color coding: colors per calendar, plus color categories/tags (Work, Personal…) | Home › Categorize | Organize Calendars › Categories |
| Multiple account sync: Google Workspace, iCloud, Exchange/Microsoft 365, Outlook.com or any ICS/webcal feed, merged into one view | Organize › Accounts / Sync Now | Organize Calendars › Accounts |
| Smart search by keyword, attendee name/e-mail or location, past or future | Home › Search, sidebar search box (Ctrl+E) | **Smart Search** |
| Reminders: pop-up window (snooze/dismiss), tray notification, sound and/or e-mail, at any lead time (15 min, 1 h, 1 day…) | Tools › Reminders / Notification Settings | Event › Reminders; **Options**: Notifications |
| To-do list: sidebar with checkboxes next to the schedule, tasks shown in Agenda view, full task manager | Home › New Task; Tools › To-Do List / Show Completed | **To-Do List**, Task |
| Focus blocks and out-of-office: invitations that overlap them are declined automatically with a message | Home › Focus Block / Out of Office; Tools › Focus Settings | **Options**: Focus & Out of Office |

Other features:

* The File menu has Import/Export `.ics`, Options, About and Exit.
* The app has a tray icon and allows only one instance.
* It is high-DPI aware and has a mini month calendar in the sidebar.

## Data and integration notes

* **Storage:** all data is stored in `%APPDATA%\WinCalendar\calendar.dat`, a UTF-8 tab-separated file. On the first run the app seeds sample calendars, events and tasks.
* **Shared calendars:** a shared calendar is a `.wcal` file in a folder all members can reach, such as a network share or a OneDrive or Dropbox folder. Members open it with *Open Shared*. Changes are saved to the file and reloaded automatically. Permissions (view/edit) are **enforced by the client only**, so use folder ACLs for real security.
* **Invitations and RSVP:** messages are sent as iCalendar (`METHOD:REQUEST/REPLY/COUNTER/CANCEL`) attachments through the default MAPI mail client, such as Outlook. If no MAPI client is installed, the app opens a `mailto:` link and saves the `.ics` file for you to attach.
  * Replies arrive as `.ics` attachments. Import them with *Open .ics Message* or *Import replies* to update the RSVP status.
  * Received invitations are imported the same way.
* **Account sync:** sync is read-only, using the account's secret iCal/ICS address (instructions for each provider appear in the dialog). It downloads over HTTPS with WinHTTP, every 30 minutes or on demand. A sync briefly blocks the UI while it downloads. Two-way CalDAV/EWS/Graph sync would require OAuth and is not implemented.
* **Meeting links:** links are generated in each provider's URL format, or taken from the personal-room links you set in *Options › Online Meetings*. Jitsi links work without an account.

## Source layout

```
Calendar.sln / Calendar.vcxproj / build.bat
src\app.h            shared declarations (model, helpers)
src\model.cpp        data model, recurrence, time zones, persistence, sample data
src\ics.cpp          iCalendar import/export, WinHTTP sync, MAPI/mailto e-mail
src\views.cpp        GDI-painted Day/Week/Month/Year/Agenda view window
src\ribbon.cpp       Windows Ribbon host (IUIApplication / IUICommandHandler)
src\commands.cpp     command routing, event services, reminders, auto-sync
src\main.cpp         main window, sidebar, tray, timers, WinMain
src\dlg_*.cpp        tabbed property-sheet dialogs
src\ribbon.xml       ribbon markup (compiled by uicc)
src\app.rc, res\     dialogs, icons, ribbon images, manifest
tools\               icon and ribbon generation scripts
```

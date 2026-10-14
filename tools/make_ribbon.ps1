# Generates src\ribbon.xml (Windows Ribbon Framework markup) from a compact command table.
#   powershell -ExecutionPolicy Bypass -File tools\make_ribbon.ps1
$cmds = @(
 # Symbol, Id, Label, Image, Keytip, Tooltip
 @('IDM_NEWEVENT',1001,'New Event','newevent','N','Create a new appointment with title, location, date and duration.'),
 @('IDM_NEWMEETING',1002,'New Meeting','newmeeting','M','Create a meeting, invite attendees and attach an online meeting link.'),
 @('IDM_NEWTASK',1003,'New Task','newtask','K','Add an item to your to-do list.'),
 @('IDM_NEWFOCUS',1004,'Focus Block','focus','F','Reserve a deep-work block that automatically declines conflicting invitations.'),
 @('IDM_NEWOOO',1005,'Out of Office','ooo','O','Mark days as out of office; new invitations during this time are auto-declined.'),
 @('IDM_OPEN',1010,'Open','open','E','Open the selected event (Enter).'),
 @('IDM_DELETE',1011,'Delete','delete','D','Delete the selected event or occurrence (Del).'),
 @('IDM_CATEGORIZE',1012,'Categorize','categorize','C','Assign a color category to the selected event.'),
 @('IDM_TODAY',1020,'Today','today','T','Go to today.'),
 @('IDM_PREV',1021,'Back','prev','B','Previous day/week/month/year (Page Up).'),
 @('IDM_NEXT',1022,'Forward','next','W','Next day/week/month/year (Page Down).'),
 @('IDM_GOTO',1023,'Go to Date','goto','G','Jump to a specific date.'),
 @('IDM_VIEWDAY',1030,'Day','viewday','1','Day view.'),
 @('IDM_VIEWWEEK',1031,'Week','viewweek','2','Week view.'),
 @('IDM_VIEWMONTH',1032,'Month','viewmonth','3','Month view.'),
 @('IDM_VIEWYEAR',1033,'Year','viewyear','4','Year view (heat map of busy days).'),
 @('IDM_VIEWAGENDA',1034,'Agenda','viewagenda','5','Agenda / list view of upcoming events and tasks.'),
 @('IDM_SEARCH',1040,'Search','search','S','Smart search by keyword, attendee or location across past and future events.'),
 @('IDM_ATTENDEES',1050,'Attendees','attendees','A','Edit the attendee list of the selected meeting.'),
 @('IDM_SENDINVITES',1051,'Send Invitations','sendinvites','I','E-mail iCalendar invitations to all attendees.'),
 @('IDM_RSVPTRACKER',1052,'RSVP Tracker','rsvp','R','See who accepted, declined, or proposed a new time.'),
 @('IDM_INVITATIONS',1053,'Invitations','invitations','V','Invitations you have received and need to answer.'),
 @('IDM_ACCEPT',1054,'Accept','accept','Y','Accept the selected invitation.'),
 @('IDM_TENTATIVE',1055,'Tentative','tentative','U','Tentatively accept the selected invitation.'),
 @('IDM_DECLINE',1056,'Decline','decline','X','Decline the selected invitation.'),
 @('IDM_PROPOSE',1057,'Propose New Time','propose','P','Propose a different time to the organizer.'),
 @('IDM_ADDTEAMS',1060,'Teams Meeting','teams','MT','Attach a Microsoft Teams link to the selected event.'),
 @('IDM_ADDZOOM',1061,'Zoom Meeting','zoom','MZ','Attach a Zoom link to the selected event.'),
 @('IDM_ADDMEET',1062,'Google Meet','meet','MG','Attach a Google Meet link to the selected event.'),
 @('IDM_ADDJITSI',1063,'Jitsi Meet','jitsi','MJ','Attach a free Jitsi Meet room (works without an account).'),
 @('IDM_JOIN',1064,'Join Online','join','J','Open the online meeting link of the selected event.'),
 @('IDM_IMPORTINVITE',1070,'Open .ics Message','importinvite','IM','Process a received invitation, reply (RSVP), counter-proposal or cancellation (.ics).'),
 @('IDM_CALENDARS',1080,'Calendars','calendars','L','Create, rename, color and show/hide calendars.'),
 @('IDM_NEWCALENDAR',1081,'New Calendar','newcalendar','NC','Create a new calendar for a project, family or client.'),
 @('IDM_SHARING',1082,'Share Calendar','sharing','SH','Share a calendar with a group (view or edit permission).'),
 @('IDM_OPENSHARED',1083,'Open Shared','openshared','OS','Open a calendar someone shared with you.'),
 @('IDM_CATEGORIES',1084,'Categories','categories','CG','Manage color categories and tags.'),
 @('IDM_ACCOUNTS',1085,'Accounts','accounts','AC','Connect Google, iCloud, Exchange or other calendar feeds.'),
 @('IDM_SYNC',1086,'Sync Now','sync','SY','Synchronize all accounts and shared calendars now.'),
 @('IDM_IMPORT',1087,'Import','import','IP','Import events from an iCalendar (.ics) file.'),
 @('IDM_EXPORT',1088,'Export','export','EX','Export events to an iCalendar (.ics) file.'),
 @('IDM_TIMEZONES',1090,'Time Zones','timezones','Z','Display time zone, secondary time zone and travel settings.'),
 @('IDM_AUTOTZ',1091,'Use My Location','autotz','LO','Automatically follow the Windows time zone of your current location.'),
 @('IDM_REMINDERS',1100,'Reminders','reminders','RM','Show active reminders.'),
 @('IDM_NOTIFYSETTINGS',1101,'Notification Settings','notifysettings','NS','Default reminders, pop-up and e-mail alerts.'),
 @('IDM_TESTNOTIFY',1102,'Test Notification','testnotify','TN','Show a sample notification.'),
 @('IDM_FOCUSSETTINGS',1110,'Focus Settings','focussettings','FS','Auto-decline settings for focus blocks and out-of-office.'),
 @('IDM_TASKS',1120,'To-Do List','tasks','TD','Manage all tasks.'),
 @('IDM_SHOWCOMPLETED',1121,'Show Completed','showcompleted','SC','Show completed tasks in the to-do list.'),
 @('IDM_OPTIONS',1130,'Options','options','OP','Application options.'),
 @('IDM_EXIT',1131,'Exit','exit','X','Close WinCalendar.'),
 @('IDM_ABOUT',1132,'About','about','H','About WinCalendar.'),
 @('IDM_HELP',1133,'Help','about','F1','About WinCalendar and keyboard shortcuts.')
)
$tabs = @(
 @('tabHome','Home','H', @(
   @('grpNew','New','ThreeButtons',@('IDM_NEWEVENT','IDM_NEWMEETING','IDM_NEWTASK')),
   @('grpManage','Manage','ThreeButtons',@('IDM_OPEN','IDM_DELETE','IDM_CATEGORIZE')),
   @('grpGoTo','Go To','FourButtons',@('IDM_TODAY','IDM_PREV','IDM_NEXT','IDM_GOTO')),
   @('grpArrange','Arrange','FiveButtons',@('T:IDM_VIEWDAY','T:IDM_VIEWWEEK','T:IDM_VIEWMONTH','T:IDM_VIEWYEAR','T:IDM_VIEWAGENDA')),
   @('grpFind','Find','OneButton',@('IDM_SEARCH')))),
 @('tabMeetings','Meetings','M', @(
   @('grpAttendees','Invite','ThreeButtons',@('IDM_ATTENDEES','IDM_SENDINVITES','IDM_RSVPTRACKER')),
   @('grpRespond','Respond','FourButtons',@('IDM_ACCEPT','IDM_TENTATIVE','IDM_DECLINE','IDM_PROPOSE')),
   @('grpOnline','Online Meeting','FiveButtons',@('IDM_ADDTEAMS','IDM_ADDZOOM','IDM_ADDMEET','IDM_ADDJITSI','IDM_JOIN')),
   @('grpInbox','Inbox','TwoButtons',@('IDM_INVITATIONS','IDM_IMPORTINVITE')))),
 @('tabOrganize','Organize','O', @(
   @('grpCalendars','Calendars','FourButtons',@('IDM_CALENDARS','IDM_NEWCALENDAR','IDM_SHARING','IDM_OPENSHARED')),
   @('grpCategories','Categories','TwoButtons',@('IDM_CATEGORIES','IDM_CATEGORIZE')),
   @('grpAccounts','Accounts','TwoButtons',@('IDM_ACCOUNTS','IDM_SYNC')),
   @('grpData','Data','TwoButtons',@('IDM_IMPORT','IDM_EXPORT')))),
 @('tabTools','Tools','T', @(
   @('grpTz','Time Zones','TwoButtons',@('IDM_TIMEZONES','T:IDM_AUTOTZ')),
   @('grpNotify','Notifications','ThreeButtons',@('IDM_REMINDERS','IDM_NOTIFYSETTINGS','IDM_TESTNOTIFY')),
   @('grpFocus','Focus & Away','ThreeButtons',@('IDM_NEWFOCUS','IDM_NEWOOO','IDM_FOCUSSETTINGS')),
   @('grpTasks','Tasks','ThreeButtons',@('IDM_TASKS','IDM_NEWTASK','T:IDM_SHOWCOMPLETED')),
   @('grpOptions','Options','OneButton',@('IDM_OPTIONS'))))
)
function X([string]$s) { [Security.SecurityElement]::Escape($s) }
$sb = New-Object Text.StringBuilder
[void]$sb.AppendLine('<?xml version="1.0" encoding="utf-8"?>')
[void]$sb.AppendLine('<!-- Generated by tools\make_ribbon.ps1 - Windows Ribbon Framework markup for WinCalendar -->')
[void]$sb.AppendLine('<Application xmlns="http://schemas.microsoft.com/windows/2009/Ribbon">')
[void]$sb.AppendLine('  <Application.Commands>')
foreach ($c in $cmds) {
  $name = 'cmd' + $c[0].Substring(4)
  [void]$sb.AppendLine("    <Command Name=""$name"" Symbol=""$($c[0])"" Id=""$($c[1])"" LabelTitle=""$(X $c[2])"" Keytip=""$($c[4])"" TooltipTitle=""$(X $c[2])"" TooltipDescription=""$(X $c[5])"">")
  [void]$sb.AppendLine("      <Command.LargeImages><Image>res/$($c[3])32.bmp</Image></Command.LargeImages>")
  [void]$sb.AppendLine("      <Command.SmallImages><Image>res/$($c[3])16.bmp</Image></Command.SmallImages>")
  [void]$sb.AppendLine('    </Command>')
}
[void]$sb.AppendLine('    <Command Name="cmdAppMenu" Symbol="IDM_APPMENU" Id="900"/>')
[void]$sb.AppendLine('    <Command Name="cmdQat" Symbol="IDM_QAT" Id="901"/>')
$gid = 910
foreach ($t in $tabs) {
  [void]$sb.AppendLine("    <Command Name=""$($t[0])"" Symbol=""IDT_$($t[0].ToUpper())"" Id=""$gid"" LabelTitle=""$($t[1])"" Keytip=""$($t[2])""/>"); $gid++
  foreach ($gr in $t[3]) { [void]$sb.AppendLine("    <Command Name=""$($gr[0])"" Symbol=""IDG_$($gr[0].ToUpper())"" Id=""$gid"" LabelTitle=""$(X $gr[1])""/>"); $gid++ }
}
[void]$sb.AppendLine('  </Application.Commands>')
[void]$sb.AppendLine('  <Application.Views>')
[void]$sb.AppendLine('    <Ribbon>')
[void]$sb.AppendLine('      <Ribbon.ApplicationMenu>')
[void]$sb.AppendLine('        <ApplicationMenu CommandName="cmdAppMenu">')
[void]$sb.AppendLine('          <MenuGroup Class="MajorItems">')
foreach ($m in 'NEWEVENT','NEWMEETING','NEWTASK','IMPORT','EXPORT','SYNC','OPTIONS') { [void]$sb.AppendLine("            <Button CommandName=""cmd$m""/>") }
[void]$sb.AppendLine('          </MenuGroup>')
[void]$sb.AppendLine('          <MenuGroup><Button CommandName="cmdABOUT"/><Button CommandName="cmdEXIT"/></MenuGroup>')
[void]$sb.AppendLine('        </ApplicationMenu>')
[void]$sb.AppendLine('      </Ribbon.ApplicationMenu>')
[void]$sb.AppendLine('      <Ribbon.HelpButton><HelpButton CommandName="cmdHELP"/></Ribbon.HelpButton>')
[void]$sb.AppendLine('      <Ribbon.QuickAccessToolbar>')
[void]$sb.AppendLine('        <QuickAccessToolbar CommandName="cmdQat">')
[void]$sb.AppendLine('          <QuickAccessToolbar.ApplicationDefaults>')
foreach ($m in 'NEWEVENT','TODAY','SEARCH','SYNC') { [void]$sb.AppendLine("            <Button CommandName=""cmd$m""/>") }
[void]$sb.AppendLine('          </QuickAccessToolbar.ApplicationDefaults>')
[void]$sb.AppendLine('        </QuickAccessToolbar>')
[void]$sb.AppendLine('      </Ribbon.QuickAccessToolbar>')
[void]$sb.AppendLine('      <Ribbon.Tabs>')
foreach ($t in $tabs) {
  [void]$sb.AppendLine("        <Tab CommandName=""$($t[0])"">")
  foreach ($gr in $t[3]) {
    [void]$sb.AppendLine("          <Group CommandName=""$($gr[0])"" SizeDefinition=""$($gr[2])"">")
    foreach ($b in $gr[3]) {
      if ($b.StartsWith('T:')) { [void]$sb.AppendLine("            <ToggleButton CommandName=""cmd$($b.Substring(6))""/>") }
      else { [void]$sb.AppendLine("            <Button CommandName=""cmd$($b.Substring(4))""/>") }
    }
    [void]$sb.AppendLine('          </Group>')
  }
  [void]$sb.AppendLine('        </Tab>')
}
[void]$sb.AppendLine('      </Ribbon.Tabs>')
[void]$sb.AppendLine('    </Ribbon>')
[void]$sb.AppendLine('  </Application.Views>')
[void]$sb.AppendLine('</Application>')
[IO.File]::WriteAllText((Join-Path $PSScriptRoot '..\src\ribbon.xml'), $sb.ToString(), (New-Object Text.UTF8Encoding $false))
Write-Host 'Generated src\ribbon.xml'

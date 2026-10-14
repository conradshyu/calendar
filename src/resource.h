// WinCalendar resource identifiers
#pragma once
#include "ribbon_ids.h"

#define IDI_APP                 1
#define IDR_ACCEL               2

// Extra commands (accelerators / context menus)
#define IDM_NEWALLDAY           1200
#define IDM_COPYLINK            1201
#define IDM_RESENDINVITES       1202
#define IDM_FOCUSSEARCH         1203
#define IDM_CAT_NONE            4999
#define IDM_CAT_FIRST           5000

// Main window controls
#define IDC_VIEW                200
#define IDC_STATUS              201
#define IDC_QSEARCH             202
#define IDC_MONTHCAL            203
#define IDC_CALLIST             204
#define IDC_TASKLIST            205
#define IDC_LBL_CALS            206
#define IDC_LBL_TASKS           207
#define IDC_ADDTASK             208

// Dialog templates
#define IDD_EV_GENERAL          101
#define IDD_EV_RECUR            102
#define IDD_EV_TZ               103
#define IDD_EV_ATTENDEES        104
#define IDD_EV_ONLINE           105
#define IDD_EV_REMIND           106
#define IDD_OPT_GENERAL         111
#define IDD_OPT_TZ              112
#define IDD_OPT_NOTIFY          113
#define IDD_OPT_ONLINE          114
#define IDD_OPT_FOCUS           115
#define IDD_ORG_CALENDARS       121
#define IDD_ORG_SHARING         122
#define IDD_ORG_CATEGORIES      123
#define IDD_ORG_ACCOUNTS        124
#define IDD_MTG_SENT            131
#define IDD_MTG_RECEIVED        132
#define IDD_SEARCH              141
#define IDD_TASK                142
#define IDD_TASKS               143
#define IDD_GOTO                144
#define IDD_PROPOSE             145
#define IDD_REMINDER            146

// Generic dialog controls (reused across templates; unique within each template)
#define IDC_TITLE               1001
#define IDC_LOCATION            1002
#define IDC_CALENDAR            1003
#define IDC_CATEGORY            1004
#define IDC_ALLDAY              1005
#define IDC_STARTDATE           1006
#define IDC_STARTTIME           1007
#define IDC_ENDDATE             1008
#define IDC_ENDTIME             1009
#define IDC_BUSY                1010
#define IDC_DESC                1011
#define IDC_SUMMARY             1012
#define IDC_TZNOTE              1013
#define IDC_READONLY            1014

#define IDC_FREQ                1020
#define IDC_INTERVAL            1021
#define IDC_INTERVAL_SPIN       1022
#define IDC_UNITS               1023
#define IDC_WD0                 1024   // ..IDC_WD0+6
#define IDC_MONTH_DAY           1031
#define IDC_MONTH_NTH           1032
#define IDC_NTH                 1033
#define IDC_NTHDAY              1034
#define IDC_END_NEVER           1035
#define IDC_END_UNTIL           1036
#define IDC_END_COUNT           1037
#define IDC_UNTILDATE           1038
#define IDC_COUNT               1039
#define IDC_COUNT_SPIN          1040
#define IDC_WEEKDAYS            1041
#define IDC_FIRSTMON            1042

#define IDC_TZ_PINNED           1050
#define IDC_TZ_FLOATING         1051
#define IDC_TZCOMBO             1052
#define IDC_TZCONVERT           1053
#define IDC_TZPREVIEW           1054
#define IDC_TZMINE              1055

#define IDC_ORGANIZER           1060
#define IDC_ATTLIST             1061
#define IDC_ATTNAME             1062
#define IDC_ATTEMAIL            1063
#define IDC_ATTOPTIONAL         1064
#define IDC_ATTADD              1065
#define IDC_ATTREMOVE           1066
#define IDC_ATTSTATUS           1067
#define IDC_ATTSETSTATUS        1068
#define IDC_SENDINVITES         1069
#define IDC_MYRESPONSE          1070

#define IDC_CONFPROVIDER        1080
#define IDC_CONFLINK            1081
#define IDC_CONFGENERATE        1082
#define IDC_CONFJOIN            1083
#define IDC_CONFCOPY            1084
#define IDC_CONFINLOC           1085
#define IDC_CONFHINT            1086

#define IDC_REMLIST             1090
#define IDC_REMTIME             1091
#define IDC_REMMETHOD           1092
#define IDC_REMADD              1093
#define IDC_REMREMOVE           1094
#define IDC_AUTODECLINE         1095
#define IDC_DECLINEMSG          1096

#define IDC_USERNAME            1100
#define IDC_USEREMAIL           1101
#define IDC_WEEKSTART           1102
#define IDC_WORKSTART           1103
#define IDC_WORKEND             1104
#define IDC_DEFDURATION         1105
#define IDC_DEFCALENDAR         1106
#define IDC_DEFVIEW             1107
#define IDC_AUTOTZ              1110
#define IDC_SYSTZ               1111
#define IDC_SHOW2TZ             1112
#define IDC_TZ2                 1113
#define IDC_DEFREMINDER         1120
#define IDC_REMPOPUP            1121
#define IDC_REMSOUND            1122
#define IDC_REMEMAIL            1123
#define IDC_TESTNOTIFY          1124
#define IDC_DEFCONF             1130
#define IDC_AUTOCONF            1131
#define IDC_ROOM1               1132   // ..IDC_ROOM1+4 (Teams, Zoom, Meet, Webex, Jitsi)
#define IDC_FOCUSLEN            1140
#define IDC_BLOCKLIST           1141
#define IDC_NEWFOCUS            1142
#define IDC_NEWOOO              1143
#define IDC_OPENBLOCK           1144

#define IDC_LIST                1150
#define IDC_LIST2               1151
#define IDC_NAME                1152
#define IDC_COLOR               1153
#define IDC_NEW                 1154
#define IDC_RENAME              1155
#define IDC_DELETE              1156
#define IDC_SETDEFAULT          1157
#define IDC_COLORSWATCH         1158
#define IDC_SHAREPATH           1160
#define IDC_BROWSE              1161
#define IDC_OWNER               1162
#define IDC_EMAIL               1163
#define IDC_PERM                1164
#define IDC_ADD                 1165
#define IDC_REMOVE              1166
#define IDC_PUBLISH             1167
#define IDC_EMAILSHARE          1168
#define IDC_OPENSHARED          1169
#define IDC_STOPSHARE           1170
#define IDC_PROVIDER            1171
#define IDC_URL                 1172
#define IDC_AUTOSYNC            1173
#define IDC_UPDATE              1174
#define IDC_SYNCNOW             1175
#define IDC_HINT                1176

#define IDC_ACCEPTPROP          1180
#define IDC_RESEND              1181
#define IDC_IMPORTREPLY         1182
#define IDC_ACCEPT              1183
#define IDC_TENTATIVE           1184
#define IDC_DECLINE             1185
#define IDC_PROPOSE             1186
#define IDC_OPEN                1187

#define IDC_KEYWORD             1190
#define IDC_SCOPE_TITLE         1191
#define IDC_SCOPE_LOC           1192
#define IDC_SCOPE_ATT           1193
#define IDC_RANGE               1194
#define IDC_SEARCHBTN           1195
#define IDC_GOTO                1196
#define IDC_RESULTCOUNT         1197

#define IDC_NOTES               1200
#define IDC_HASDUE              1201
#define IDC_DUEDATE             1202
#define IDC_PRIORITY            1203
#define IDC_DONE                1204
#define IDC_EDIT                1205
#define IDC_CLEARDONE           1206
#define IDC_SHOWDONE            1207

#define IDC_DATE                1210
#define IDC_TIME                1211
#define IDC_COMMENT             1212
#define IDC_SNOOZETIME          1213
#define IDC_SNOOZE              1214
#define IDC_DISMISS             1215
#define IDC_DISMISSALL          1216

; SpeedyNote Inno Setup Script
#define MyAppVersion "1.0.1"

[Setup]
AppName=SpeedyNote
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\SpeedyNote
DefaultGroupName=SpeedyNote
OutputBaseFilename=SpeedyNoteInstaller_{#MyAppVersion}_amd64
Compression=lzma
SolidCompression=yes
OutputDir=Output
DisableProgramGroupPage=yes
WizardStyle=modern

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[CustomMessages]
; Task Group Descriptions
en.AdditionalIcons=Additional icons:
es.AdditionalIcons=Iconos adicionales:
zh.AdditionalIcons=附加图标：
fr.AdditionalIcons=Icônes supplémentaires :

en.FileAssociations=File associations:
es.FileAssociations=Asociaciones de archivos:
zh.FileAssociations=文件关联：
fr.FileAssociations=Associations de fichiers :

; Task Descriptions
en.DesktopIconTask=Create a desktop shortcut
es.DesktopIconTask=Crear un acceso directo en el escritorio
zh.DesktopIconTask=创建桌面快捷方式
fr.DesktopIconTask=Créer un raccourci sur le bureau

en.PDFAssociationTask=Associate PDF files with SpeedyNote (adds SpeedyNote to 'Open with' menu)
es.PDFAssociationTask=Asociar archivos PDF con SpeedyNote (agrega SpeedyNote al menú 'Abrir con')
zh.PDFAssociationTask=将 PDF 文件与 SpeedyNote 关联（将 SpeedyNote 添加到"打开方式"菜单）
fr.PDFAssociationTask=Associer les fichiers PDF avec SpeedyNote (ajoute SpeedyNote au menu 'Ouvrir avec')

[Files]
Source: ".\build\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\SpeedyNote"; Filename: "{app}\NoteApp.exe"; WorkingDir: "{app}"
Name: "{commondesktop}\SpeedyNote"; Filename: "{app}\NoteApp.exe"; WorkingDir: "{app}"; IconFilename: "{app}\NoteApp.exe"; Tasks: desktopicon
Name: "{group}\Uninstall SpeedyNote"; Filename: "{uninstallexe}"

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIconTask}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "pdfassociation"; Description: "{cm:PDFAssociationTask}"; GroupDescription: "{cm:FileAssociations}"

[Registry]
; =============================================================================
; CLEANUP: Remove legacy .spn file association (no longer used in v1.0.0+)
; These run unconditionally during install to clean up old versions
; =============================================================================
Root: HKCR; Subkey: ".spn"; Flags: deletekey
Root: HKCR; Subkey: "SpeedyNote.SPN"; Flags: deletekey
Root: HKCR; Subkey: ".spn\ShellNew"; Flags: deletekey
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote\Capabilities\FileAssociations"; ValueName: ".spn"; Flags: deletevalue

; =============================================================================
; PDF file association entries (only if task is selected)
; uninsdeletekey: removes entire key tree on uninstall
; uninsdeletevalue: removes only the specific value on uninstall
; =============================================================================

; Application registration (uninsdeletekey on root removes entire tree)
Root: HKCR; Subkey: "Applications\NoteApp.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "SpeedyNote"; Tasks: pdfassociation; Flags: uninsdeletekey
Root: HKCR; Subkey: "Applications\NoteApp.exe\DefaultIcon"; ValueType: string; ValueData: "{app}\NoteApp.exe,0"; Tasks: pdfassociation
Root: HKCR; Subkey: "Applications\NoteApp.exe\shell\open"; ValueType: string; ValueName: "FriendlyName"; ValueData: "Open with SpeedyNote"; Tasks: pdfassociation
Root: HKCR; Subkey: "Applications\NoteApp.exe\shell\open\command"; ValueType: string; ValueData: """{app}\NoteApp.exe"" ""%1"""; Tasks: pdfassociation

; PDF OpenWith entries (uninsdeletekey/uninsdeletevalue for shared keys)
Root: HKCR; Subkey: ".pdf\OpenWithList\NoteApp.exe"; Tasks: pdfassociation; Flags: uninsdeletekey
Root: HKCR; Subkey: ".pdf\OpenWithProgids"; ValueType: string; ValueName: "SpeedyNote.PDF"; ValueData: ""; Tasks: pdfassociation; Flags: uninsdeletevalue

; SpeedyNote.PDF ProgID (uninsdeletekey on root removes entire tree)
Root: HKCR; Subkey: "SpeedyNote.PDF"; ValueType: string; ValueData: "PDF Document (SpeedyNote)"; Tasks: pdfassociation; Flags: uninsdeletekey
Root: HKCR; Subkey: "SpeedyNote.PDF"; ValueType: string; ValueName: "FriendlyTypeName"; ValueData: "PDF Document for SpeedyNote"; Tasks: pdfassociation
Root: HKCR; Subkey: "SpeedyNote.PDF\DefaultIcon"; ValueType: string; ValueData: "{app}\NoteApp.exe,0"; Tasks: pdfassociation
Root: HKCR; Subkey: "SpeedyNote.PDF\shell\open"; ValueType: string; ValueName: "FriendlyName"; ValueData: "Open with SpeedyNote"; Tasks: pdfassociation
Root: HKCR; Subkey: "SpeedyNote.PDF\shell\open\command"; ValueType: string; ValueData: """{app}\NoteApp.exe"" ""%1"""; Tasks: pdfassociation

; Windows Registered Applications (uninsdeletevalue for shared key, uninsdeletekey for our tree)
Root: HKLM; Subkey: "SOFTWARE\RegisteredApplications"; ValueType: string; ValueName: "SpeedyNote"; ValueData: "SOFTWARE\SpeedyNote\Capabilities"; Tasks: pdfassociation; Flags: uninsdeletevalue
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote"; ValueType: string; ValueName: ""; ValueData: ""; Tasks: pdfassociation; Flags: uninsdeletekey
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "SpeedyNote"; Tasks: pdfassociation
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Fast Digital Note Taking Application with PDF Support"; Tasks: pdfassociation
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "SpeedyNote.PDF"; Tasks: pdfassociation

[Run]
Filename: "{app}\NoteApp.exe"; Description: "{cm:LaunchProgram,SpeedyNote}"; Flags: nowait postinstall skipifsilent

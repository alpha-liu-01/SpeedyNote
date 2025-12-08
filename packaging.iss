; SpeedyNote Inno Setup Script
#define MyAppVersion "0.11.3"

[Setup]
AppName=SpeedyNote
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\SpeedyNote
DefaultGroupName=SpeedyNote
OutputBaseFilename=SpeedyNoteInstaller_Beta_{#MyAppVersion}_amd64
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

en.ContextMenuGroup=Context menu:
es.ContextMenuGroup=Menú contextual:
zh.ContextMenuGroup=右键菜单：
fr.ContextMenuGroup=Menu contextuel :

; Task Descriptions
en.DesktopIconTask=Create a desktop shortcut
es.DesktopIconTask=Crear un acceso directo en el escritorio
zh.DesktopIconTask=创建桌面快捷方式
fr.DesktopIconTask=Créer un raccourci sur le bureau

en.PDFAssociationTask=Associate PDF files with SpeedyNote (adds SpeedyNote to 'Open with' menu)
es.PDFAssociationTask=Asociar archivos PDF con SpeedyNote (agrega SpeedyNote al menú 'Abrir con')
zh.PDFAssociationTask=将 PDF 文件与 SpeedyNote 关联（将 SpeedyNote 添加到"打开方式"菜单）
fr.PDFAssociationTask=Associer les fichiers PDF avec SpeedyNote (ajoute SpeedyNote au menu 'Ouvrir avec')

en.SPNAssociationTask=Associate .spn files with SpeedyNote (SpeedyNote Package files)
es.SPNAssociationTask=Asociar archivos .spn con SpeedyNote (archivos de paquete de SpeedyNote)
zh.SPNAssociationTask=将 .spn 文件与 SpeedyNote 关联（SpeedyNote 包文件）
fr.SPNAssociationTask=Associer les fichiers .spn avec SpeedyNote (fichiers de package SpeedyNote)

en.ContextMenuTask=Add 'SpeedyNote Package' to right-click 'New' menu
es.ContextMenuTask=Agregar 'Paquete de SpeedyNote' al menú 'Nuevo' del clic derecho
zh.ContextMenuTask=将"SpeedyNote 包"添加到右键"新建"菜单
fr.ContextMenuTask=Ajouter 'Package SpeedyNote' au menu 'Nouveau' du clic droit

[Files]
Source: ".\build\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\SpeedyNote"; Filename: "{app}\NoteApp.exe"; WorkingDir: "{app}"
Name: "{commondesktop}\SpeedyNote"; Filename: "{app}\NoteApp.exe"; WorkingDir: "{app}"; IconFilename: "{app}\NoteApp.exe"; Tasks: desktopicon
Name: "{group}\Uninstall SpeedyNote"; Filename: "{uninstallexe}"

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIconTask}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "pdfassociation"; Description: "{cm:PDFAssociationTask}"; GroupDescription: "{cm:FileAssociations}"
Name: "spnassociation"; Description: "{cm:SPNAssociationTask}"; GroupDescription: "{cm:FileAssociations}"
Name: "contextmenu"; Description: "{cm:ContextMenuTask}"; GroupDescription: "{cm:ContextMenuGroup}"

[Registry]
; PDF file association entries (only if task is selected)
Root: HKCR; Subkey: "Applications\NoteApp.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "SpeedyNote"; Tasks: pdfassociation
Root: HKCR; Subkey: "Applications\NoteApp.exe\DefaultIcon"; ValueType: string; ValueData: "{app}\NoteApp.exe,0"; Tasks: pdfassociation
Root: HKCR; Subkey: "Applications\NoteApp.exe\shell\open"; ValueType: string; ValueName: "FriendlyName"; ValueData: "Open with SpeedyNote"; Tasks: pdfassociation
Root: HKCR; Subkey: "Applications\NoteApp.exe\shell\open\command"; ValueType: string; ValueData: """{app}\NoteApp.exe"" ""%1"""; Tasks: pdfassociation
Root: HKCR; Subkey: ".pdf\OpenWithList\NoteApp.exe"; Tasks: pdfassociation
Root: HKCR; Subkey: ".pdf\OpenWithProgids"; ValueType: string; ValueName: "SpeedyNote.PDF"; ValueData: ""; Tasks: pdfassociation
Root: HKCR; Subkey: "SpeedyNote.PDF"; ValueType: string; ValueData: "PDF Document (SpeedyNote)"; Tasks: pdfassociation
Root: HKCR; Subkey: "SpeedyNote.PDF"; ValueType: string; ValueName: "FriendlyTypeName"; ValueData: "PDF Document for SpeedyNote"; Tasks: pdfassociation
Root: HKCR; Subkey: "SpeedyNote.PDF\DefaultIcon"; ValueType: string; ValueData: "{app}\NoteApp.exe,0"; Tasks: pdfassociation
Root: HKCR; Subkey: "SpeedyNote.PDF\shell\open"; ValueType: string; ValueName: "FriendlyName"; ValueData: "Open with SpeedyNote"; Tasks: pdfassociation
Root: HKCR; Subkey: "SpeedyNote.PDF\shell\open\command"; ValueType: string; ValueData: """{app}\NoteApp.exe"" ""%1"""; Tasks: pdfassociation
Root: HKLM; Subkey: "SOFTWARE\RegisteredApplications"; ValueType: string; ValueName: "SpeedyNote"; ValueData: "SOFTWARE\SpeedyNote\Capabilities"; Tasks: pdfassociation
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "SpeedyNote"; Tasks: pdfassociation
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Fast Digital Note Taking Application with PDF Support"; Tasks: pdfassociation
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "SpeedyNote.PDF"; Tasks: pdfassociation

; ✅ SPN file association entries (only if task is selected)
Root: HKCR; Subkey: ".spn"; ValueType: string; ValueData: "SpeedyNote.SPN"; Tasks: spnassociation
Root: HKCR; Subkey: "SpeedyNote.SPN"; ValueType: string; ValueData: "SpeedyNote Package"; Tasks: spnassociation
Root: HKCR; Subkey: "SpeedyNote.SPN"; ValueType: string; ValueName: "FriendlyTypeName"; ValueData: "SpeedyNote Package File"; Tasks: spnassociation
Root: HKCR; Subkey: "SpeedyNote.SPN\DefaultIcon"; ValueType: string; ValueData: "{app}\NoteApp.exe,0"; Tasks: spnassociation
Root: HKCR; Subkey: "SpeedyNote.SPN\shell\open"; ValueType: string; ValueName: "FriendlyName"; ValueData: "Open with SpeedyNote"; Tasks: spnassociation
Root: HKCR; Subkey: "SpeedyNote.SPN\shell\open\command"; ValueType: string; ValueData: """{app}\NoteApp.exe"" ""%1"""; Tasks: spnassociation
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote\Capabilities\FileAssociations"; ValueType: string; ValueName: ".spn"; ValueData: "SpeedyNote.SPN"; Tasks: spnassociation

; ✅ Context menu "New" entry for .spn files
Root: HKCR; Subkey: ".spn\ShellNew"; ValueType: string; ValueName: "Command"; ValueData: """{app}\NoteApp.exe"" --create-silent ""%1"""; Tasks: contextmenu
Root: HKCR; Subkey: ".spn\ShellNew"; ValueType: string; ValueName: "MenuText"; ValueData: "SpeedyNote Package"; Tasks: contextmenu
Root: HKCR; Subkey: ".spn\ShellNew"; ValueType: string; ValueName: "IconPath"; ValueData: "{app}\NoteApp.exe,0"; Tasks: contextmenu

[Run]
Filename: "{app}\NoteApp.exe"; Description: "{cm:LaunchProgram,SpeedyNote}"; Flags: nowait postinstall skipifsilent

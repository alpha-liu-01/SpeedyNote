; SpeedyNote Inno Setup Script
#define MyAppVersion "1.3.0"

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
; Required for modifying PATH environment variable
ChangesEnvironment=yes

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

en.SystemIntegration=System integration:
es.SystemIntegration=Integración del sistema:
zh.SystemIntegration=系统集成：
fr.SystemIntegration=Intégration système :

; Task Descriptions
en.DesktopIconTask=Create a desktop shortcut
es.DesktopIconTask=Crear un acceso directo en el escritorio
zh.DesktopIconTask=创建桌面快捷方式
fr.DesktopIconTask=Créer un raccourci sur le bureau

en.PDFAssociationTask=Associate PDF files with SpeedyNote (adds SpeedyNote to 'Open with' menu)
es.PDFAssociationTask=Asociar archivos PDF con SpeedyNote (agrega SpeedyNote al menú 'Abrir con')
zh.PDFAssociationTask=将 PDF 文件与 SpeedyNote 关联（将 SpeedyNote 添加到"打开方式"菜单）
fr.PDFAssociationTask=Associer les fichiers PDF avec SpeedyNote (ajoute SpeedyNote au menu 'Ouvrir avec')

en.AddToPathTask=Add SpeedyNote to PATH (enables 'speedynote' command in terminal)
es.AddToPathTask=Agregar SpeedyNote a PATH (habilita el comando 'speedynote' en la terminal)
zh.AddToPathTask=将 SpeedyNote 添加到 PATH（在终端中启用 'speedynote' 命令）
fr.AddToPathTask=Ajouter SpeedyNote au PATH (active la commande 'speedynote' dans le terminal)

[Files]
Source: ".\build\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\SpeedyNote"; Filename: "{app}\speedynote.exe"; WorkingDir: "{app}"
Name: "{commondesktop}\SpeedyNote"; Filename: "{app}\speedynote.exe"; WorkingDir: "{app}"; IconFilename: "{app}\speedynote.exe"; Tasks: desktopicon
Name: "{group}\Uninstall SpeedyNote"; Filename: "{uninstallexe}"

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIconTask}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "pdfassociation"; Description: "{cm:PDFAssociationTask}"; GroupDescription: "{cm:FileAssociations}"
Name: "addtopath"; Description: "{cm:AddToPathTask}"; GroupDescription: "{cm:SystemIntegration}"

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
Root: HKCR; Subkey: "Applications\speedynote.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "SpeedyNote"; Tasks: pdfassociation; Flags: uninsdeletekey
Root: HKCR; Subkey: "Applications\speedynote.exe\DefaultIcon"; ValueType: string; ValueData: "{app}\speedynote.exe,0"; Tasks: pdfassociation
Root: HKCR; Subkey: "Applications\speedynote.exe\shell\open"; ValueType: string; ValueName: "FriendlyName"; ValueData: "Open with SpeedyNote"; Tasks: pdfassociation
Root: HKCR; Subkey: "Applications\speedynote.exe\shell\open\command"; ValueType: string; ValueData: """{app}\speedynote.exe"" ""%1"""; Tasks: pdfassociation

; PDF OpenWith entries (uninsdeletekey/uninsdeletevalue for shared keys)
Root: HKCR; Subkey: ".pdf\OpenWithList\speedynote.exe"; Tasks: pdfassociation; Flags: uninsdeletekey
Root: HKCR; Subkey: ".pdf\OpenWithProgids"; ValueType: string; ValueName: "SpeedyNote.PDF"; ValueData: ""; Tasks: pdfassociation; Flags: uninsdeletevalue

; SpeedyNote.PDF ProgID (uninsdeletekey on root removes entire tree)
Root: HKCR; Subkey: "SpeedyNote.PDF"; ValueType: string; ValueData: "PDF Document (SpeedyNote)"; Tasks: pdfassociation; Flags: uninsdeletekey
Root: HKCR; Subkey: "SpeedyNote.PDF"; ValueType: string; ValueName: "FriendlyTypeName"; ValueData: "PDF Document for SpeedyNote"; Tasks: pdfassociation
Root: HKCR; Subkey: "SpeedyNote.PDF\DefaultIcon"; ValueType: string; ValueData: "{app}\speedynote.exe,0"; Tasks: pdfassociation
Root: HKCR; Subkey: "SpeedyNote.PDF\shell\open"; ValueType: string; ValueName: "FriendlyName"; ValueData: "Open with SpeedyNote"; Tasks: pdfassociation
Root: HKCR; Subkey: "SpeedyNote.PDF\shell\open\command"; ValueType: string; ValueData: """{app}\speedynote.exe"" ""%1"""; Tasks: pdfassociation

; Windows Registered Applications (uninsdeletevalue for shared key, uninsdeletekey for our tree)
Root: HKLM; Subkey: "SOFTWARE\RegisteredApplications"; ValueType: string; ValueName: "SpeedyNote"; ValueData: "SOFTWARE\SpeedyNote\Capabilities"; Tasks: pdfassociation; Flags: uninsdeletevalue
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote"; ValueType: string; ValueName: ""; ValueData: ""; Tasks: pdfassociation; Flags: uninsdeletekey
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "SpeedyNote"; Tasks: pdfassociation
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Fast Digital Note Taking Application with PDF Support"; Tasks: pdfassociation
Root: HKLM; Subkey: "SOFTWARE\SpeedyNote\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "SpeedyNote.PDF"; Tasks: pdfassociation

[UninstallDelete]
; Remove the speedynote.cmd wrapper created during installation
Type: files; Name: "{app}\speedynote.cmd"

[Run]
Filename: "{app}\speedynote.exe"; Description: "{cm:LaunchProgram,SpeedyNote}"; Flags: nowait postinstall skipifsilent

[Code]
// =============================================================================
// PATH Environment Variable Management
// =============================================================================
// Adds/removes SpeedyNote installation directory to/from user PATH
// This enables 'speedynote' command from any terminal window
// =============================================================================

const
  EnvironmentKey = 'Environment';

// Check if a directory is already in PATH
function IsInPath(const Dir: string): Boolean;
var
  Path: string;
begin
  Result := False;
  if RegQueryStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Path) then
  begin
    // Check for exact match (case-insensitive, with and without trailing backslash)
    Result := (Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Path) + ';') > 0) or
              (Pos(';' + Uppercase(Dir) + '\;', ';' + Uppercase(Path) + ';') > 0);
  end;
end;

// Add directory to user PATH
procedure AddToPath(const Dir: string);
var
  Path: string;
begin
  if not IsInPath(Dir) then
  begin
    if not RegQueryStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Path) then
      Path := '';
    
    // Append with semicolon separator
    if (Path <> '') and (Path[Length(Path)] <> ';') then
      Path := Path + ';';
    Path := Path + Dir;
    
    RegWriteExpandStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Path);
  end;
end;

// Remove directory from user PATH
procedure RemoveFromPath(const Dir: string);
var
  Path, NewPath: string;
  P: Integer;
  DirUpper: string;
begin
  if RegQueryStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Path) then
  begin
    NewPath := Path;
    DirUpper := Uppercase(Dir);
    
    // Remove all occurrences (with and without trailing backslash)
    // Handle: ;Dir; , ;Dir\; , Dir; (at start), ;Dir (at end)
    while True do
    begin
      P := Pos(';' + DirUpper + ';', ';' + Uppercase(NewPath) + ';');
      if P > 0 then
      begin
        // Found in middle or end
        if P = 1 then
          // At start: remove "Dir;" 
          NewPath := Copy(NewPath, Length(Dir) + 2, Length(NewPath))
        else
          // In middle or end: remove ";Dir"
          NewPath := Copy(NewPath, 1, P - 1) + Copy(NewPath, P + Length(Dir) + 1, Length(NewPath));
      end
      else
      begin
        // Try with trailing backslash
        P := Pos(';' + DirUpper + '\;', ';' + Uppercase(NewPath) + ';');
        if P > 0 then
        begin
          if P = 1 then
            NewPath := Copy(NewPath, Length(Dir) + 3, Length(NewPath))
          else
            NewPath := Copy(NewPath, 1, P - 1) + Copy(NewPath, P + Length(Dir) + 2, Length(NewPath));
        end
        else
          Break; // No more occurrences
      end;
    end;
    
    // Clean up any double semicolons or trailing semicolon
    while Pos(';;', NewPath) > 0 do
      StringChangeEx(NewPath, ';;', ';', True);
    if (Length(NewPath) > 0) and (NewPath[Length(NewPath)] = ';') then
      NewPath := Copy(NewPath, 1, Length(NewPath) - 1);
    if (Length(NewPath) > 0) and (NewPath[1] = ';') then
      NewPath := Copy(NewPath, 2, Length(NewPath));
    
    if NewPath <> Path then
      RegWriteExpandStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', NewPath);
  end;
end;

// Create speedynote.cmd wrapper script for CLI access
procedure CreateCmdWrapper();
var
  CmdPath: string;
  CmdContent: string;
begin
  CmdPath := ExpandConstant('{app}\speedynote.cmd');
  CmdContent := '@echo off' + #13#10 + '"%~dp0speedynote.exe" %*';
  SaveStringToFile(CmdPath, CmdContent, False);
end;

// Called after installation
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if IsTaskSelected('addtopath') then
    begin
      // Create the speedynote.cmd wrapper script
      CreateCmdWrapper();
      // Add installation directory to PATH
      AddToPath(ExpandConstant('{app}'));
    end;
  end;
end;

// Called during uninstallation
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    // Always try to remove from PATH on uninstall
    RemoveFromPath(ExpandConstant('{app}'));
  end;
end;

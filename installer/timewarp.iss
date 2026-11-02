; Inno Setup script for Flintstones/Jetsons Timewarp (CD-i static recompilation).
; Built by tools\package.ps1, which passes AppVersion and StageDir:
;   ISCC.exe /DAppVersion=1.0.0 /DStageDir=..\dist\Timewarp-1.0.0 installer\timewarp.iss
;
; The game keeps everything it writes (saves, settings, quicksaves, log) in
; its own folder, so it installs per user, without administrator rights, to a
; folder the user can write to.  The disc image is not part of the package:
; a wizard page asks where it is and writes the location to timewarp.ini.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\dist\Timewarp-" + AppVersion
#endif
#define AppName "Flintstones/Jetsons Timewarp"
#define AppFolder "Flintstones-Jetsons Timewarp"

[Setup]
AppId={{6E0F4C1B-3A57-4B7E-9C2D-5A1E8F0B7D42}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=tools81
AppPublisherURL=https://github.com/tools81/flintstones-jetsons-timewarp-static-recomp
AppSupportURL=https://github.com/tools81/flintstones-jetsons-timewarp-static-recomp/issues
AppUpdatesURL=https://github.com/tools81/flintstones-jetsons-timewarp-static-recomp/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={%USERPROFILE}\Games\{#AppFolder}
DefaultGroupName={#AppFolder}
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=Timewarp-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
UninstallDisplayIcon={app}\timewarp_hd.exe
UninstallDisplayName={#AppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for the HD edition"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName} HD"; Filename: "{app}\timewarp_hd.exe"; WorkingDir: "{app}"; Comment: "Cartoon HD upscaling, run-ahead, sprite interpolation, quicksave"
Name: "{group}\{#AppName} (original)"; Filename: "{app}\timewarp.exe"; WorkingDir: "{app}"; Comment: "The game as the CD-i showed it"
Name: "{group}\Read me"; Filename: "{app}\README.md"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName} HD"; Filename: "{app}\timewarp_hd.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\timewarp_hd.exe"; WorkingDir: "{app}"; Description: "Play {#AppName} HD now"; Flags: nowait postinstall skipifsilent

[Code]
var
  DiscPage: TInputDirWizardPage;

{ the folder (or its rom subfolder) holds a .bin with a CD-i disc label }
function FolderHasDisc(Dir: String): Boolean;
var
  FindRec: TFindRec;
begin
  Result := False;
  if (Dir = '') or not DirExists(Dir) then Exit;
  if FindFirst(AddBackslash(Dir) + '*.bin', FindRec) then begin
    Result := True;
    FindClose(FindRec);
  end;
end;

{ the location given when installing before (or nothing) }
function PreviousDisc(): String;
var
  Lines: TArrayOfString;
  I: Integer;
begin
  Result := '';
  if LoadStringsFromFile(ExpandConstant('{app}\timewarp.ini'), Lines) then
    for I := 0 to GetArrayLength(Lines) - 1 do
      if Pos('rom=', Lowercase(Lines[I])) = 1 then
        Result := Copy(Lines[I], 5, Length(Lines[I]));
end;

procedure InitializeWizard();
begin
  DiscPage := CreateInputDirPage(wpSelectDir,
    'Game disc', 'Where is your Flintstones/Jetsons Timewarp disc image?',
    'The game reads its pictures, sound and animation from your copy of the CD-i disc. ' +
    'It is not included in this package.' + #13#10#13#10 +
    'Select the folder that contains the disc image: the .cue file and its .bin ' +
    '(raw 2352-byte sectors, for example "Flintstones-Jetsons Timewarp (USA).bin").' + #13#10#13#10 +
    'The suggested folder is the rom folder inside the game''s folder: keep it to copy the disc ' +
    'image there later. If the game cannot find the disc when it starts, it asks for it.',
    False, '');
  DiscPage.Add('Disc image folder:');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = DiscPage.ID) and (DiscPage.Values[0] = '') then begin
    DiscPage.Values[0] := PreviousDisc();
    if DiscPage.Values[0] = '' then DiscPage.Values[0] := ExpandConstant('{app}\rom');
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir: String;
begin
  Result := True;
  if CurPageID <> DiscPage.ID then Exit;
  Dir := Trim(DiscPage.Values[0]);
  { the suggested folder inside the game's folder: the disc can be copied there later }
  if CompareText(Dir, ExpandConstant('{app}\rom')) = 0 then Exit;
  if not FolderHasDisc(Dir) and FolderHasDisc(AddBackslash(Dir) + 'rom') then begin
    DiscPage.Values[0] := AddBackslash(Dir) + 'rom';
    Exit;
  end;
  if not FolderHasDisc(Dir) then
    Result := MsgBox('No .bin disc image was found in' + #13#10 + Dir + #13#10#13#10 +
                     'Use this folder anyway? (If the game cannot find the disc, it asks for it.)',
                     mbConfirmation, MB_YESNO) = IDYES;
end;

{ timewarp.ini: rom=<folder>; the other settings already there are kept }
procedure WriteSettings();
var
  Lines, NewLines: TArrayOfString;
  I, N: Integer;
  Ini, Dir: String;
  HasUpdate: Boolean;
begin
  Ini := ExpandConstant('{app}\timewarp.ini');
  Dir := Trim(DiscPage.Values[0]);
  if CompareText(Dir, ExpandConstant('{app}\rom')) = 0 then Dir := '';   { the game looks there anyway }
  N := 0;
  HasUpdate := False;
  if LoadStringsFromFile(Ini, Lines) then
    for I := 0 to GetArrayLength(Lines) - 1 do begin
      if (Dir <> '') and (Pos('rom=', Lowercase(Lines[I])) = 1) then Continue;
      if Pos('update_check=', Lowercase(Lines[I])) = 1 then HasUpdate := True;
      SetArrayLength(NewLines, N + 1);
      NewLines[N] := Lines[I];
      N := N + 1;
    end;
  if Dir <> '' then begin
    SetArrayLength(NewLines, N + 1);
    NewLines[N] := 'rom=' + Dir;
    N := N + 1;
  end;
  if not HasUpdate then begin
    SetArrayLength(NewLines, N + 1);
    NewLines[N] := 'update_check=1';
    N := N + 1;
  end;
  SaveStringsToFile(Ini, NewLines, False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then WriteSettings();
end;

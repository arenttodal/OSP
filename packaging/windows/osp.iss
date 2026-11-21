; ANDOR OSP test installer for Windows (64-bit), built by .github/workflows/windows-test.yml.
; Not signed: Windows SmartScreen may warn ("More info" > "Run anyway").
; The plugin's identity is the macOS build's (VST3 class, parameters, state): sessions move
; between the two.
;
; Input (from the workflow): stage\OSP.vst3 (the VST3 bundle, its factory packs inside),
; stage\OSP.exe (the standalone), stage\Factory (the factory packs, for the standalone).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6F1C7B2E-4D7A-4C1E-9B1F-0A8D2C5E7F31}
AppName=ANDOR OSP
AppVersion={#AppVersion}
AppVerName=ANDOR OSP {#AppVersion} (test build)
AppPublisher=ANDOR
DefaultDirName={autopf}\ANDOR\OSP
DefaultGroupName=ANDOR OSP
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=output
OutputBaseFilename=OSP-Windows-test-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=ANDOR OSP (test build)
InfoBeforeFile=README.txt

[Types]
Name: "full"; Description: "VST3 plug-in and standalone app"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Common Files\VST3\OSP.vst3)"; Types: full custom
Name: "standalone"; Description: "Standalone app (Start menu: ANDOR OSP)"; Types: full custom

[Files]
Source: "stage\OSP.vst3\*"; DestDir: "{commoncf64}\VST3\OSP.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
; The standalone finds its factory packs at <its folder>\..\Resources\Factory.
Source: "stage\OSP.exe"; DestDir: "{app}\bin"; Flags: ignoreversion; Components: standalone
Source: "stage\Factory\*"; DestDir: "{app}\Resources\Factory"; Flags: recursesubdirs createallsubdirs ignoreversion skipifsourcedoesntexist; Components: standalone
Source: "README.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\ANDOR OSP"; Filename: "{app}\bin\OSP.exe"; Components: standalone
Name: "{group}\Uninstall ANDOR OSP"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\bin\OSP.exe"; Description: "Open ANDOR OSP now"; Flags: postinstall nowait skipifsilent unchecked; Components: standalone

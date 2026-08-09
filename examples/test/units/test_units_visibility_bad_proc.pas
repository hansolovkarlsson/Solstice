program TestUnitsVisibilityBadProc;

// PrivateHelper is only declared in VisLib.pas's implementation section
// - calling it directly from the main program is a clean compile
// error, not a crash.

uses VisLib;

begin
  PrivateHelper;
end.

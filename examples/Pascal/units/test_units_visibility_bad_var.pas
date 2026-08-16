program TestUnitsVisibilityBadVar;

// PrivateCounter is only declared in VisLib.pas's implementation
// section - reading it directly from the main program is a clean
// compile error, not a crash.

uses VisLib;

begin
  writeln(PrivateCounter);
end.

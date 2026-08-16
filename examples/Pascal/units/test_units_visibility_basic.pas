program TestUnitsVisibilityBasic;

// VisLib.pas's implementation section privately declares PrivateCounter
// (var) and PrivateHelper (proc), both used only by its own
// interface-exported DoWork - confirms a unit's own code can see its
// own private declarations, and the publicly visible behavior is
// correct. Each DoWork call bumps PrivateCounter (starts at 0) and
// PublicCounter (starts at 0) by 1 first.
// DoWork(5) = 5*2 + 1 = 11. DoWork(3) = 3*2 + 2 = 8. Final PublicCounter,
// bumped once per call, = 2.

uses VisLib;

begin
  writeln(DoWork(5));
  writeln(DoWork(3));
  writeln(PublicCounter);
end.

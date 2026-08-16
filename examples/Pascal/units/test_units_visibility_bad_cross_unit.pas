program TestUnitsVisibilityBadCrossUnit;

// VisLibUser.pas (uses VisLib) tries to call VisLib's implementation-
// only PrivateHelper from its OWN code - confirms visibility is
// genuinely scoped to the declaring unit, not just "hidden from the
// main program specifically". Clean compile error, not a crash.

uses VisLibUser;

begin
  TryPrivate;
end.

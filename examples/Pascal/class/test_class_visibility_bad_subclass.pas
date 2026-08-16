program TestClassVisibilityBadSubclass;

// A subclass's own method tries to access an ancestor's private field
// - a clean compile error, not a crash. Confirms strict, non-inherited
// semantics: private means only the DECLARING class's own methods,
// not even a subclass's - matching real Pascal/Delphi's 'private' (as
// opposed to 'protected', which isn't in scope here).

type
  TBase = class
  private
    secret: integer;
  end;
  TSub = class(TBase)
    procedure TryAccess;
  end;

var s: TSub;

procedure TSub.TryAccess;
begin
  secret := 5;
end;

begin
  new(s);
  s.TryAccess;
  dispose(s);
end.

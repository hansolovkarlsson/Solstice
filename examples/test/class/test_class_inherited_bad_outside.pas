program TestInheritedBadOutside;

// 'inherited' used outside any class method body (here, an ordinary
// top-level procedure) - clean compile error, not a crash.

procedure TopLevel;
begin
  inherited;
end;

begin
  TopLevel;
end.

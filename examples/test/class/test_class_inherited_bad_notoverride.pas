program TestInheritedBadNotoverride;

// Bare 'inherited;' inside a method that's newly introduced by this
// class (not overriding anything the parent declares) - clean compile
// error, not a crash.

type
  TBase = class
    procedure Old;
  end;
  TSub = class(TBase)
    procedure New1;
  end;

var s: TSub;

procedure TBase.Old;
begin
end;

procedure TSub.New1;
begin
  inherited;
end;

begin
  new(s);
  s.New1;
  dispose(s);
end.

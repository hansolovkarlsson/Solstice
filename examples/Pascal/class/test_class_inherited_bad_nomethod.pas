program TestInheritedBadNomethod;

// 'inherited SomeName(...)' naming a method the parent doesn't have -
// clean compile error, not a crash.

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
  inherited DoesNotExist();
end;

begin
  new(s);
  s.New1;
  dispose(s);
end.

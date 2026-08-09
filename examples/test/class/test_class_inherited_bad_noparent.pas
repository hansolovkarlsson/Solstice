program TestInheritedBadNoparent;

// 'inherited' used inside a method of a class with no parent - clean
// compile error, not a crash.

type
  TFoo = class
    procedure Bar;
  end;

var f: TFoo;

procedure TFoo.Bar;
begin
  inherited;
end;

begin
  new(f);
  f.Bar;
  dispose(f);
end.

program TestVisibilityBadConstructor;

// External code calls 'new(c, Init(args))' where Init is private - a
// clean compile error, not a crash.

type
  TFoo = class
    val: integer;
  private
    procedure Init(v: integer);
  end;

var f: TFoo;

procedure TFoo.Init;
begin
  val := v;
end;

begin
  new(f, Init(1));
  dispose(f);
end.

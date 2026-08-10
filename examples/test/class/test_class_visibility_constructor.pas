program TestClassVisibilityConstructor;

// 'new(c, Init(args))' where Init is private - not a special case,
// checked the same way an ordinary method call is (a completely
// separate lookup path in parse_new_statement(), not routed through
// resolve_heap_deref_step() at all, so it needs its own identical
// check). Called from within the class's own code, including
// constructing a brand-new OTHER instance of itself from inside a
// method. f.val = 1 (via the public SetupSelf wrapper), other.val = 99
// (via a direct new(..., Init(...)) call inside MakeAnother).

type
  TFoo = class
    val: integer;
  private
    procedure Init(v: integer);
  public
    procedure SetupSelf(v: integer);
    procedure MakeAnother;
  end;

var f: TFoo;

procedure TFoo.Init;
begin
  val := v;
end;

procedure TFoo.SetupSelf;
begin
  Init(v);
end;

procedure TFoo.MakeAnother;
var other: TFoo;
begin
  new(other, Init(99));
  writeln(other.val);
  dispose(other);
end;

begin
  new(f);
  f.SetupSelf(1);
  writeln(f.val);
  f.MakeAnother;
  dispose(f);
end.

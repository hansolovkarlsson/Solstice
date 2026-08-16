program TestVisibilityCrossInstance;

// A method of TVec reads ANOTHER TVec instance's private field
// directly (via the global 'b', not through self) - confirms privacy
// is per-CLASS, not per-instance: any method of TVec can access any
// TVec instance's private fields, matching real Pascal/Delphi
// semantics. a.SameAsB is TRUE when both are 5, FALSE once b becomes 9.

type
  TVec = class
  private
    x: integer;
  public
    procedure SetX(v: integer);
    function SameAsB: boolean;
  end;

var a, b: TVec;

procedure TVec.SetX;
begin
  x := v;
end;

function TVec.SameAsB;
begin
  SameAsB := x = b.x;
end;

begin
  new(a);
  new(b);
  a.SetX(5);
  b.SetX(5);
  writeln(a.SameAsB);
  b.SetX(9);
  writeln(a.SameAsB);
  dispose(a);
  dispose(b);
end.

program TestInheritedFunction;

// 'inherited' used in EXPRESSION position, not just as a statement -
// both the explicit-name form and bare 'inherited;' return a usable
// value rather than being discarded. TA.GetVal = 5.
// TB.GetVal := (inherited;) + 1 = 6.

type
  TA = class
    function GetVal: integer;
  end;
  TB = class(TA)
    function GetVal: integer;
  end;

var b: TB;
    x: integer;

function TA.GetVal;
begin
  GetVal := 5;
end;

function TB.GetVal;
begin
  x := inherited;
  GetVal := x + 1;
end;

begin
  new(b);
  writeln(b.GetVal);
  dispose(b);
end.

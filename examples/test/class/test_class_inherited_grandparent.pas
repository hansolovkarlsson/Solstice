program TestInheritedGrandparent;

// A three-level hierarchy where the IMMEDIATE parent doesn't override
// the method itself - only the grandparent does. 'inherited' resolves
// against the parent's already-flattened methods[], which correctly
// reflects the grandparent's implementation without needing to walk
// the hierarchy any further at compile time. TA.Value = 7,
// TC.Value := inherited Value() * 2 = 14.

type
  TA = class
    function Value: integer;
  end;
  TB = class(TA)
  end;
  TC = class(TB)
    function Value: integer;
  end;

var c: TC;

function TA.Value;
begin
  Value := 7;
end;

function TC.Value;
begin
  Value := inherited Value() * 2;
end;

begin
  new(c);
  writeln(c.Value);
  dispose(c);
end.

program TestRelational;
var
    x, y, z : integer;
    is_equal, is_less, is_greater : boolean;
begin
    { Initialize integer values }
    x := 10;
    y := 20;
    
    { Evaluate arithmetic expressions }
    z := x + 10;

    { Perform relational comparison evaluations }
    is_less := x < y;         { Should evaluate to true (1) }
    is_greater := y > z;      { Should evaluate to false (0) }
    is_equal := x = 10;       { Should evaluate to true (1) }
end.

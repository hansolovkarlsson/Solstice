program DceTest;
var
  x, y, active : integer;
begin
  x := 45;            { Dead: assigned but never read }
  y := 10 + 2;        { Alive: read to calculate 'active' }
  active := y * 2;    { Alive: read? No! Wait, active isn't read either! }
end.

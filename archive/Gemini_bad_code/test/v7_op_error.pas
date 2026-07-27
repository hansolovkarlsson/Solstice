program OpError;
var
  flag : boolean;
  ans  : integer;
begin
  flag := false;
  ans := 10 + flag; { Error! Cannot perform addition on a boolean value }
end.

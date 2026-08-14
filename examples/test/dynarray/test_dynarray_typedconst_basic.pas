program TestDArrTypedConstBasic;
{ A BARE dynamic-array typed constant (not inside a record) - see
  docs/LANGUAGE.md#typed-constants-record-initializers. }
const
    X: array of integer = [10, 20, 30];
var
    i, total: integer;
begin
    writeln(Length(X));                { 3 }
    writeln(X[0], ' ', X[1], ' ', X[2]);  { 10 20 30 }
    total := 0;
    for i in X do total := total + i;
    writeln(total);                    { 60 }
end.

program TestDArrTypedConstEmpty;
{ An empty array literal ('[]') is a valid BARE dynamic-array typed
  constant value, same as any other array-literal assignment. }
const
    X: array of integer = [];
begin
    writeln(Length(X));                { 0 }
end.

program TestDArrFieldTCBasic;
{ A dynamic-array field's own value in a typed constant, given via
  array-literal syntax - see docs/LANGUAGE.md#record-and-class-fields. }
type
    TScores = record
        name: string;
        values: array of integer;
    end;
const
    Bob: TScores = (name: 'Bob'; values: [10, 20, 30]);
var
    i, total: integer;
begin
    writeln(Bob.name);
    writeln(Length(Bob.values));       { 3 }
    writeln(Bob.values[0], ' ', Bob.values[1], ' ', Bob.values[2]);  { 10 20 30 }
    total := 0;
    for i in Bob.values do total := total + i;
    writeln(total);                    { 60 }
end.

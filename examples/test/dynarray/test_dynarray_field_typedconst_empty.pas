program TestDArrFieldTCEmpty;
{ An empty array literal ('[]') is a valid dynamic-array field value in
  a typed constant, same as any other array-literal assignment. }
type
    TBox = record
        data: array of integer;
    end;
const
    Empty: TBox = (data: []);
begin
    writeln(Length(Empty.data));       { 0 }
end.

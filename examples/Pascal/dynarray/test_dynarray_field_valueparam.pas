program TestDynArrayFieldValueParam;
type
    TBox = record
        data: array of integer;
    end;
var
    box: TBox;

procedure Touch(b: TBox);
begin
    b.data[0] := 999;      { element write shares storage - visible to the caller }
    SetLength(b.data, 5);   { rebinds only the LOCAL copy of the field - never escapes }
end;

begin
    SetLength(box.data, 2);
    box.data[0] := 1;
    box.data[1] := 2;
    Touch(box);
    writeln(Length(box.data));  { 2 - SetLength inside Touch didn't escape }
    writeln(box.data[0]);        { 999 - the element write did }
end.

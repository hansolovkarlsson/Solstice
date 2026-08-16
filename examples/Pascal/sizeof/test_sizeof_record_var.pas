program TestSizeofRecordVar;
type
    TRec = record
        flag: byte;
        total: integer;
    end;
var
    globalR: TRec;

procedure CheckLocal;
var
    localR: TRec;
begin
    writeln(sizeOf(localR));
end;

begin
    writeln(sizeOf(globalR));
    CheckLocal;
end.

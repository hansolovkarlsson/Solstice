program TestClassArrayfieldInherit;
type
    TBase = class
        vals: array[0..2] of integer;
    end;
    TDerived = class(TBase)
        { never redeclares 'vals' - purely inherited. The subclass's own
          flattened fields[] must still compute the correct offset. }
        extra: integer;
    end;
var
    d: TDerived;
begin
    new(d);
    d.vals[0] := 5;
    d.vals[1] := 6;
    d.vals[2] := 7;
    d.extra := 99;

    writeln('vals: ', d.vals[0], ' ', d.vals[1], ' ', d.vals[2]);
    writeln('extra: ', d.extra);

    dispose(d);
end.

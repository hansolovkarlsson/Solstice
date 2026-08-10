program TestIsasNilIs;
type
    TFoo = class
    public
        x: integer;
    end;
var
    p: TFoo;
begin
    p := nil;
    writeln('nil is TFoo: ', p is TFoo);
    writeln('program continues normally');
end.

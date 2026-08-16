program TestSealedBadInherit;
type
    TFoo = class sealed
        x: integer;
    end;
    TBar = class(TFoo)
        y: integer;
    end;
var
    b: TBar;
begin
    new(b);
end.

program TestConstTypedRecordUsage;

{ A record typed constant's field passed as an ordinary argument, and
  compared - confirms it reads exactly like any other record variable's
  field once initialized. sum=15 is_origin=false }

type
    TPoint = record
        x, y: integer;
    end;

const
    Base: TPoint = (x: 6; y: 9);

function Sum(a, b: integer): integer;
begin
    Sum := a + b;
end;

begin
    writeln('sum=', Sum(Base.x, Base.y));
    if (Base.x = 0) and (Base.y = 0) then
        writeln('is_origin=true')
    else
        writeln('is_origin=false');
end.

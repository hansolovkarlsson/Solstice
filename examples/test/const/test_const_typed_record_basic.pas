program TestConstTypedRecordBasic;

{ Basic record typed constant: fields in declaration order.
  x=1 y=2 }

type
    TPoint = record
        x, y: integer;
    end;

const
    Origin: TPoint = (x: 1; y: 2);

begin
    writeln('x=', Origin.x, ' y=', Origin.y);
end.

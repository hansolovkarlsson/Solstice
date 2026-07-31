program TestEnumScope;
type
    TColor = (Red, Green, Blue);
    TShade = TColor;
    TPoint = record
        x, y: integer;
        color: TColor;
    end;
var
    p: TPoint;
    palette: array[1..3] of TColor;
    s: TShade;
    i: integer;

function nextColor(c: TColor): TColor;
var
    temp: TColor;
begin
    temp := succ(c);
    nextColor := temp;
end;

begin
    p.x := 1;
    p.y := 2;
    p.color := Blue;
    writeln('p.color = ', p.color);

    palette[1] := Red;
    palette[2] := nextColor(Red);
    palette[3] := nextColor(palette[2]);
    for i := 1 to 3 do
        writeln('palette[', i, '] = ', palette[i]);

    s := Green;
    writeln('s = ', s);
end.

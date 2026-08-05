program TestRecarrBasic;
{ Records as array elements: field read/write via a runtime index, plus
  whole-element copy in all three supported directions (array-to-array,
  array-to-plain-record, plain-record-to-array). Expected output:
  pts[2]=(5,10)
  pts[3]=(100,200)
  p=(5,10)
  pts[1]=(5,10)
  pts[2]=(5,10)
  pts[3]=(100,200) }
type
    TPoint = record
        x, y: integer;
    end;
var
    pts: array[1..3] of TPoint;
    p: TPoint;
    i: integer;
begin
    pts[1].x := 5;
    pts[1].y := 10;

    pts[2] := pts[1]; { array element <- array element }
    writeln('pts[2]=(', pts[2].x, ',', pts[2].y, ')');

    p.x := 100;
    p.y := 200;
    pts[3] := p; { array element <- plain record }
    writeln('pts[3]=(', pts[3].x, ',', pts[3].y, ')');

    p := pts[1]; { plain record <- array element }
    writeln('p=(', p.x, ',', p.y, ')');

    for i := 1 to 3 do
        writeln('pts[', i, ']=(', pts[i].x, ',', pts[i].y, ')');
end.

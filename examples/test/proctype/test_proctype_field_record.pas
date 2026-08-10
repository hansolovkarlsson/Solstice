program TestProctypeFieldRecord;

// A plain (non-class) record field of a named procedural type already
// worked with zero changes - record field assignment already routed
// through the ordinary "procedural-type assignment target" machinery
// every plain var/local assignment uses. Kept as a regression check,
// since the fixes needed for CLASS fields (a completely different,
// heap-based access mechanism) easily could have broken this instead.
// Double(5) = 10.

type
  TProc = function(x: integer): integer;
  TRec = record
    name: integer;
    handler: TProc;
  end;

var r: TRec;

function Double(x: integer): integer;
begin
  Double := x * 2;
end;

begin
  r.name := 1;
  r.handler := Double;
  writeln(r.handler(5));
end.

program TestLambdaField;

{ Assign a lambda literal to a record field of procedural type.
  Double(5) = 10. }

type
    TProc = function(x: integer): integer;
    TRec = record
        name: integer;
        handler: TProc;
    end;

var r: TRec;

begin
    r.name := 1;
    r.handler := function(x: integer): integer begin exit(x * 2); end;
    writeln(r.handler(5));
end.

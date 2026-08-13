program TestLambdaProcedure;

{ A procedure-typed (no return value) lambda literal. }

type
    TPrinter = procedure(x: integer);

var
    p: TPrinter;

begin
    p := procedure(x: integer) begin writeln('got: ', x); end;
    p(42);
end.

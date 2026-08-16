program TestDirectivesElse;
{$DEFINE DEFINED_ONE}
begin
{$IFDEF UNDEFINED_ONE}
    writeln('should not print');
{$ELSE}
    writeln('else ran');
{$ENDIF}
{$IFDEF DEFINED_ONE}
    writeln('if ran');
{$ELSE}
    writeln('should not print');
{$ENDIF}
end.

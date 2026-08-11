program TestDirectivesBadNoEndif;
{$IFDEF FOO}
begin
    writeln('never gets here');
end.

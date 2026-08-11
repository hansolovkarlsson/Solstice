program TestDirectivesDeadCodeSkipped;
begin
{$IFDEF NEVER_DEFINED}
    ThisIdentifierDoesNotExistAtAll := +++ ;;; garbage nonsense here;
{$ENDIF}
    writeln('compiled fine despite garbage in dead branch');
end.

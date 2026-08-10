program TestPropertyPublicFrontsPrivate;
type
    TCircle = class
    private
        FRadius: real;
        procedure SetRadius(r: real);
    public
        { Radius is PUBLIC even though both its backing field and its
          setter are PRIVATE - property-level visibility governs access,
          not the visibility of whatever it routes through. }
        property Radius: real read FRadius write SetRadius;
    end;
var
    c: TCircle;

procedure TCircle.SetRadius;
begin
    FRadius := r;
end;

begin
    new(c);
    { Accessed from outside the class entirely (the main program block,
      not any TCircle method) - would fail if the underlying private
      field/method's own visibility were consulted instead of the
      property's. }
    c.Radius := 9.0;
    writeln('Radius = ', c.Radius:0:2);
    dispose(c);
end.

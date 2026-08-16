program TestDeclorderConstTypeConst;

{ Three interleaved blocks: 'const', then 'type', then 'const' again -
  the second const (Favorite) references an enum VALUE declared in the
  'type' block just before it, which could never have compiled before
  this compiler supported interleaving ('type' always parsed strictly
  after 'const', so an enum value could never exist yet at any 'const'
  section). TotalBonus, in that same later const block, also reaches
  back to the FIRST const block's BaseBonus - already legal before this
  change (const sections were adjacent), kept here just to show a
  single later block can draw on both an earlier const AND an earlier
  type at once. favorite is green
  15 }

const
    BaseBonus = 10;

type
    TColor = (Red, Green, Blue);

const
    Favorite = Green;
    TotalBonus = BaseBonus + 5;

var
    c: TColor;

begin
    c := Favorite;
    if c = Green then
        writeln('favorite is green');
    writeln(TotalBonus);
end.

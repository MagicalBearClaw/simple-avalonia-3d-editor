namespace EditorApp.Models;

public class PrimitiveItem
{
    private static readonly string[] TypeNames = ["Cube", "Sphere", "Pyramid", "Cylinder", "Cone"];

    public int    Id          { get; }
    public int    Type        { get; }
    public string DisplayName { get; }

    public PrimitiveItem(int id, int type)
    {
        Id          = id;
        Type        = type;
        DisplayName = $"{TypeNames[type]} #{id}";
    }
}

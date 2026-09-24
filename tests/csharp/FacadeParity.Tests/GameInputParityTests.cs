using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Xml.Linq;
using Xunit;

namespace FacadeParity.Tests;

public class GameInputParityTests
{
    private static readonly Assembly Facade = typeof(GodotGameInput.GameInput).Assembly;

    public static IEnumerable<object[]> Classes() =>
        Directory.GetFiles(RepoPaths.DocClasses("godot_gameinput"), "*.xml")
            .Select(f => new object[] { Path.GetFileNameWithoutExtension(f) });

    [Theory]
    [MemberData(nameof(Classes))]
    public void NativeClassHasManagedWrapper(string nativeClass)
    {
        System.Type csharpType =
            ParityChecker.ResolveType(Facade, nativeClass, "GameInput", typeof(GodotGameInput.GameInput));
        Assert.True(csharpType != null, $"No C# facade type found for native class '{nativeClass}'.");

        string xml = Path.Combine(RepoPaths.DocClasses("godot_gameinput"), nativeClass + ".xml");
        List<string> missing = ParityChecker.FindMissingMembers(xml, csharpType);

        Assert.True(missing.Count == 0,
            $"{csharpType.FullName} is missing wrappers for native members: {string.Join(", ", missing)}");
    }

    public static IEnumerable<object[]> ClassesWithConstants() =>
        Directory.GetFiles(RepoPaths.DocClasses("godot_gameinput"), "*.xml")
            .Where(f => XDocument.Load(f).Root.Element("constants")?.Elements("constant").Any() == true)
            .Select(f => new object[] { Path.GetFileNameWithoutExtension(f) });

    // The facade's enums are hand-written copies of the native constants, so a
    // typo in a value compiles fine and misbehaves at run time.
    [Theory]
    [MemberData(nameof(ClassesWithConstants))]
    public void NativeConstantsMatchManagedEnumValues(string nativeClass)
    {
        System.Type csharpType =
            ParityChecker.ResolveType(Facade, nativeClass, "GameInput", typeof(GodotGameInput.GameInput));
        Assert.True(csharpType != null, $"No C# facade type found for native class '{nativeClass}'.");

        string xml = Path.Combine(RepoPaths.DocClasses("godot_gameinput"), nativeClass + ".xml");
        List<string> problems = ParityChecker.FindConstantMismatches(xml, csharpType);

        Assert.True(problems.Count == 0,
            $"{csharpType.FullName} enums disagree with the native constants: {string.Join("; ", problems)}");
    }
}

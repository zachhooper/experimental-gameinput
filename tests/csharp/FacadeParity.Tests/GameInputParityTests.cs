using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
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

    // A C# node that only subscribes must hear the native signals while GDScript
    // (the addon's bootstrap autoload) drives Initialize() and Poll(), so adding
    // a handler has to connect the bridge. A field-like event only stores the
    // delegate and would stay silent until something reads GameInput.Singleton.
    [Fact]
    public void AddingAnEventHandlerConnectsTheNativeBridge()
    {
        EventInfo[] events = typeof(GodotGameInput.GameInput).GetEvents(BindingFlags.Public | BindingFlags.Static);
        Assert.NotEmpty(events);

        string[] silent = events
            .Where(e => !Calls(e.AddMethod, "ConnectBridge", "get_Singleton"))
            .Select(e => e.Name)
            .ToArray();

        Assert.True(silent.Length == 0,
            $"GameInput event(s) {string.Join(", ", silent)} do not connect the native signals when a handler "
            + "is added; give each an add accessor that resolves the singleton.");
    }

    private delegate void HandlerUpdate(ref Action handlers, Action handler);

    // The add and remove accessors share these helpers. Subscribers on several
    // threads at once must not lose a handler, as with a field-like event.
    [Fact]
    public void EventHandlersAddedAndRemovedAcrossThreadsAreAllKept()
    {
        HandlerUpdate combine = HandlerHelper("CombineHandler");
        HandlerUpdate remove = HandlerHelper("RemoveHandler");
        const int Threads = 8;
        const int PerThread = 400;
        Action[][] handlers = Enumerable.Range(0, Threads)
            .Select(t => Enumerable.Range(0, PerThread)
                .Select(i => (Action)(() => GC.KeepAlive(t * PerThread + i)))
                .ToArray())
            .ToArray();

        Action shared = null;
        RunTogether(Threads, t =>
        {
            foreach (Action handler in handlers[t])
            {
                combine(ref shared, handler);
            }
        });
        Assert.Equal(Threads * PerThread, shared?.GetInvocationList().Length ?? 0);

        RunTogether(Threads, t =>
        {
            foreach (Action handler in handlers[t])
            {
                remove(ref shared, handler);
            }
        });
        Assert.Null(shared);
    }

    private static HandlerUpdate HandlerHelper(string name) =>
        typeof(GodotGameInput.GameInput)
            .GetMethod(name, BindingFlags.NonPublic | BindingFlags.Static)
            .MakeGenericMethod(typeof(Action))
            .CreateDelegate<HandlerUpdate>();

    private static void RunTogether(int threads, Action<int> body)
    {
        using var start = new Barrier(threads);
        Thread[] workers = Enumerable.Range(0, threads)
            .Select(t => new Thread(() =>
            {
                start.SignalAndWait();
                body(t);
            }))
            .ToArray();
        foreach (Thread worker in workers)
        {
            worker.Start();
        }

        foreach (Thread worker in workers)
        {
            worker.Join();
        }
    }

    private static bool Calls(MethodInfo method, params string[] names)
    {
        const byte CallOpcode = 0x28;
        byte[] il = method.GetMethodBody()?.GetILAsByteArray() ?? Array.Empty<byte>();
        for (int i = 0; i + 4 < il.Length; i++)
        {
            if (il[i] != CallOpcode)
            {
                continue;
            }

            try
            {
                if (names.Contains(method.Module.ResolveMethod(BitConverter.ToInt32(il, i + 1)).Name))
                {
                    return true;
                }
            }
            catch (ArgumentException)
            {
                // The byte was an operand, not an opcode.
            }
        }

        return false;
    }
}

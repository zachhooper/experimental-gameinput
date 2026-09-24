using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Xml.Linq;

namespace FacadeParity.Tests;

/// <summary>
/// Asserts that every method and member documented in a native addon's
/// <c>doc_classes</c> XML has a corresponding managed member on the matching C#
/// facade type. This catches API drift as the native surface evolves.
/// </summary>
internal static class ParityChecker
{
    /// <summary>
    /// Resolves the C# facade type for a native doc-class name, or null if the
    /// class is intentionally not wrapped as a distinct type.
    /// </summary>
    public static Type ResolveType(Assembly facade, string nativeClassName, string singletonNativeName,
        Type singletonType)
    {
        if (nativeClassName == singletonNativeName)
        {
            return singletonType;
        }

        // GDK native classes use a "GDK" prefix; the C# facade uses "Xbox".
        string candidate = nativeClassName.StartsWith("GDK", StringComparison.Ordinal)
            ? "Xbox" + nativeClassName.Substring(3)
            : nativeClassName;

        return facade.GetTypes().FirstOrDefault(t => t.IsPublic && t.Name == candidate)
            ?? facade.GetTypes().FirstOrDefault(t =>
                t.IsPublic && t.Name.Equals(candidate, StringComparison.OrdinalIgnoreCase));
    }

    /// <summary>
    /// Returns the set of native method/member names from the doc XML that have
    /// no matching managed member on <paramref name="csharpType"/>.
    /// </summary>
    public static List<string> FindMissingMembers(string xmlPath, Type csharpType)
    {
        XDocument doc = XDocument.Load(xmlPath);
        XElement root = doc.Root;

        var required = new List<string>();
        required.AddRange(root.Elements("methods").Elements("method")
            .Select(m => (string)m.Attribute("name")));
        required.AddRange(root.Elements("members").Elements("member")
            .Select(m => (string)m.Attribute("name")));
        required.AddRange(root.Elements("signals").Elements("signal")
            .Select(m => (string)m.Attribute("name")));

        HashSet<string> managed = ManagedMemberNames(csharpType);

        var missing = new List<string>();
        foreach (string nativeName in required.Where(n => !string.IsNullOrEmpty(n)).Distinct())
        {
            if (!IsCovered(nativeName, managed))
            {
                missing.Add(nativeName);
            }
        }

        return missing;
    }

    /// <summary>
    /// Returns the native constants in the doc XML whose managed enum member is
    /// missing or has a different value, and the other direction: managed enum
    /// members no native constant matched, and nested managed enums with no
    /// native enum at all. A constant of native enum <c>E</c> must be a member
    /// of the nested C# enum <c>E</c> on <paramref name="csharpType"/>, named
    /// without the enum's shared prefix (<c>SRC_BTN_A</c> is
    /// <c>Source.BtnA</c>): the longest managed name that ends the native name
    /// is the match.
    /// </summary>
    public static List<string> FindConstantMismatches(string xmlPath, Type csharpType)
    {
        var problems = new List<string>();
        XElement constants = XDocument.Load(xmlPath).Root.Element("constants");
        if (constants == null)
        {
            return problems;
        }

        var mirrored = new HashSet<Type>();
        foreach (IGrouping<string, XElement> group in constants.Elements("constant")
                     .GroupBy(c => (string)c.Attribute("enum") ?? string.Empty))
        {
            Type enumType = group.Key.Length == 0 ? null : csharpType.GetNestedType(group.Key, BindingFlags.Public);
            if (enumType == null || !enumType.IsEnum)
            {
                problems.Add($"no nested enum '{group.Key}' for {group.Count()} constant(s)");
                continue;
            }

            mirrored.Add(enumType);
            Dictionary<string, long> members = Enum.GetNames(enumType)
                .ToDictionary(Normalize, n => Convert.ToInt64(Enum.Parse(enumType, n)));
            var matched = new HashSet<string>();
            foreach (XElement constant in group)
            {
                string nativeName = (string)constant.Attribute("name");
                long nativeValue = long.Parse((string)constant.Attribute("value"));
                string norm = Normalize(nativeName);
                string match = members.Keys
                    .Where(m => norm.EndsWith(m, StringComparison.Ordinal))
                    .OrderByDescending(m => m.Length)
                    .FirstOrDefault();
                if (match == null)
                {
                    problems.Add($"{group.Key}.{nativeName} has no managed member");
                    continue;
                }

                matched.Add(match);
                if (members[match] != nativeValue)
                {
                    problems.Add($"{group.Key}.{nativeName} is {nativeValue} natively but {members[match]} in C#");
                }
            }

            foreach (string name in Enum.GetNames(enumType).Where(n => !matched.Contains(Normalize(n))))
            {
                problems.Add($"{group.Key}.{name} has no native constant");
            }
        }

        foreach (Type nested in csharpType.GetNestedTypes(BindingFlags.Public).Where(t => t.IsEnum && !mirrored.Contains(t)))
        {
            problems.Add($"{nested.Name} mirrors no native enum");
        }

        return problems;
    }

    private static bool IsCovered(string nativeName, HashSet<string> managed)
    {
        string norm = Normalize(nativeName);
        if (managed.Contains(norm))
        {
            return true;
        }

        // Native getters/setters map to C# properties without the prefix.
        // "has_" is deliberately excluded: a native has_x presence probe must be
        // covered by an explicit HasX() method, not by an X property, which would
        // otherwise let a missing presence accessor pass parity silently.
        foreach (string prefix in new[] { "get_", "set_", "is_" })
        {
            if (nativeName.StartsWith(prefix, StringComparison.Ordinal) &&
                managed.Contains(Normalize(nativeName.Substring(prefix.Length))))
            {
                return true;
            }
        }

        // Native bare boolean members (e.g. "guest", "valid", "enabled") map to
        // C# Is/Get/Has-prefixed properties (IsGuest, IsValid, IsEnabled).
        foreach (string prefix in new[] { "is", "get", "has" })
        {
            if (managed.Contains(prefix + norm))
            {
                return true;
            }
        }

        return false;
    }

    private static HashSet<string> ManagedMemberNames(Type type)
    {
        const BindingFlags flags = BindingFlags.Public | BindingFlags.Instance |
                                   BindingFlags.Static | BindingFlags.FlattenHierarchy;

        var names = new HashSet<string>();
        foreach (MemberInfo m in type.GetMembers(flags))
        {
            names.Add(Normalize(m.Name));
        }

        // Nested enum value names (e.g. Source.BtnA) also satisfy native constants.
        foreach (Type nested in type.GetNestedTypes(BindingFlags.Public))
        {
            names.Add(Normalize(nested.Name));
            if (nested.IsEnum)
            {
                foreach (string enumName in Enum.GetNames(nested))
                {
                    names.Add(Normalize(enumName));
                }
            }
        }

        return names;
    }

    private static string Normalize(string name) => name.Replace("_", string.Empty).ToLowerInvariant();
}

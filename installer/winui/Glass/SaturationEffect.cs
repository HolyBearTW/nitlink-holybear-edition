// Derived from Harbor 2c243bb; NitLink lifecycle and branding adaptations. GPL-3.0. See SOURCE.md.
using System.Runtime.InteropServices;
using Windows.Graphics.Effects;
using WinRT;

namespace NitLinkSetup.Glass;

/// <summary>
/// The D2D saturation effect described for the system compositor. Windows.UI.Composition reads effects through
/// <c>IGraphicsEffectD2D1Interop</c>, a classic COM interface the C# projection cannot implement, so the effect is a
/// small native COM object with hand-built vtables: <c>IGraphicsEffect</c> (also answering IUnknown, IInspectable and
/// <c>IGraphicsEffectSource</c>) at offset 0 and <c>IGraphicsEffectD2D1Interop</c> at offset 8.
/// </summary>
internal static unsafe class SaturationEffect
{
    [StructLayout(LayoutKind.Sequential)]
    struct Effect
    {
        public void** EffectVtbl, InteropVtbl;
        public int References;
        public float Amount;
        public nint Source, Name;
    }

    static readonly Guid IUnknown = new("00000000-0000-0000-C000-000000000046");
    static readonly Guid IInspectable = new("AF86E2E0-B12D-4C6A-9C5A-D7AA65101E90");
    static readonly Guid IGraphicsEffect = new("CB51C0CE-8FE6-4636-B202-861FAA07D8F3");
    static readonly Guid IGraphicsEffectSource = new("2D8F9DDC-4339-4EB9-9216-F9DEB75658A2");
    static readonly Guid IGraphicsEffectD2D1Interop = new("2FC57384-A068-44D7-A331-30982FCF7177");
    static readonly Guid IPropertyValue = new("4BD682DD-7554-40E9-9A9B-82654EDE7E62");
    static readonly Guid IPropertyValueStatics = new("629BDBC8-D932-4FF4-96B9-8D96C5C1E858");
    static readonly Guid ClsidSaturation = new("5CB2D9CF-327D-459F-A0CE-40C0B2086BF7");
    const int Ok = 0, NoInterface = unchecked((int)0x80004002), InvalidArgument = unchecked((int)0x80070057);

    static readonly void** effectVtbl = BuildEffectVtbl(), interopVtbl = BuildInteropVtbl();
    static nint propertyValues;

    /// <summary>Saturation of <paramref name="source"/> by <paramref name="amount"/> (1 leaves it unchanged).</summary>
    internal static IGraphicsEffect Create(IGraphicsEffectSource source, float amount)
    {
        var effect = (Effect*)NativeMemory.AllocZeroed((nuint)sizeof(Effect));
        effect->EffectVtbl = effectVtbl;
        effect->InteropVtbl = interopVtbl;
        effect->References = 1;
        effect->Amount = amount;
        effect->Source = MarshalInterface<IGraphicsEffectSource>.FromManaged(source);
        WindowsCreateString("Saturation", 10, out effect->Name);
        try { return MarshalInterface<IGraphicsEffect>.FromAbi((nint)effect); }
        finally { Release((nint)effect); }
    }

    static void** BuildEffectVtbl()
    {
        var v = (void**)NativeMemory.Alloc(8, (nuint)sizeof(void*));
        v[0] = (delegate* unmanaged<nint, Guid*, nint*, int>)&EffectQueryInterface;
        v[1] = (delegate* unmanaged<nint, uint>)&EffectAddRef;
        v[2] = (delegate* unmanaged<nint, uint>)&EffectRelease;
        v[3] = (delegate* unmanaged<nint, uint*, Guid**, int>)&GetIids;
        v[4] = (delegate* unmanaged<nint, nint*, int>)&GetRuntimeClassName;
        v[5] = (delegate* unmanaged<nint, int*, int>)&GetTrustLevel;
        v[6] = (delegate* unmanaged<nint, nint*, int>)&GetName;
        v[7] = (delegate* unmanaged<nint, nint, int>)&SetName;
        return v;
    }

    static void** BuildInteropVtbl()
    {
        var v = (void**)NativeMemory.Alloc(9, (nuint)sizeof(void*));
        v[0] = (delegate* unmanaged<nint, Guid*, nint*, int>)&InteropQueryInterface;
        v[1] = (delegate* unmanaged<nint, uint>)&InteropAddRef;
        v[2] = (delegate* unmanaged<nint, uint>)&InteropRelease;
        v[3] = (delegate* unmanaged<nint, Guid*, int>)&GetEffectId;
        v[4] = (delegate* unmanaged<nint, char*, uint*, int*, int>)&GetNamedPropertyMapping;
        v[5] = (delegate* unmanaged<nint, uint*, int>)&GetPropertyCount;
        v[6] = (delegate* unmanaged<nint, uint, nint*, int>)&GetProperty;
        v[7] = (delegate* unmanaged<nint, uint, nint*, int>)&GetSource;
        v[8] = (delegate* unmanaged<nint, uint*, int>)&GetSourceCount;
        return v;
    }

    static Effect* FromEffect(nint self) => (Effect*)self;
    static Effect* FromInterop(nint self) => (Effect*)(self - sizeof(nint));

    static int QueryInterface(Effect* effect, Guid* iid, nint* result)
    {
        if (*iid == IUnknown || *iid == IInspectable || *iid == IGraphicsEffect || *iid == IGraphicsEffectSource)
            *result = (nint)effect;
        else if (*iid == IGraphicsEffectD2D1Interop)
            *result = (nint)effect + sizeof(nint);
        else
        {
            *result = 0;
            return NoInterface;
        }
        Interlocked.Increment(ref effect->References);
        return Ok;
    }

    static uint Release(nint self)
    {
        var effect = (Effect*)self;
        var left = Interlocked.Decrement(ref effect->References);
        if (left == 0)
        {
            if (effect->Source != 0) Marshal.Release(effect->Source);
            WindowsDeleteString(effect->Name);
            NativeMemory.Free(effect);
        }
        return (uint)left;
    }

    [UnmanagedCallersOnly] static int EffectQueryInterface(nint self, Guid* iid, nint* result) => QueryInterface(FromEffect(self), iid, result);
    [UnmanagedCallersOnly] static uint EffectAddRef(nint self) => (uint)Interlocked.Increment(ref FromEffect(self)->References);
    [UnmanagedCallersOnly] static uint EffectRelease(nint self) => Release(self);
    [UnmanagedCallersOnly] static int InteropQueryInterface(nint self, Guid* iid, nint* result) => QueryInterface(FromInterop(self), iid, result);
    [UnmanagedCallersOnly] static uint InteropAddRef(nint self) => (uint)Interlocked.Increment(ref FromInterop(self)->References);
    [UnmanagedCallersOnly] static uint InteropRelease(nint self) => Release((nint)FromInterop(self));

    [UnmanagedCallersOnly] static int GetIids(nint self, uint* count, Guid** iids) { *count = 0; *iids = null; return Ok; }
    [UnmanagedCallersOnly] static int GetRuntimeClassName(nint self, nint* name) { *name = 0; return Ok; }
    [UnmanagedCallersOnly] static int GetTrustLevel(nint self, int* level) { *level = 0; return Ok; }
    [UnmanagedCallersOnly] static int GetName(nint self, nint* name) => WindowsDuplicateString(FromEffect(self)->Name, out *name);
    [UnmanagedCallersOnly]
    static int SetName(nint self, nint name)
    {
        var effect = FromEffect(self);
        var result = WindowsDuplicateString(name, out var copy);
        if (result != Ok) return result;
        WindowsDeleteString(effect->Name);
        effect->Name = copy;
        return Ok;
    }

    [UnmanagedCallersOnly] static int GetEffectId(nint self, Guid* id) { *id = ClsidSaturation; return Ok; }
    [UnmanagedCallersOnly]
    static int GetNamedPropertyMapping(nint self, char* name, uint* index, int* mapping)
    {
        if (new string(name) != "Saturation") return InvalidArgument;
        *index = 0;
        *mapping = 1; // GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT
        return Ok;
    }
    [UnmanagedCallersOnly] static int GetPropertyCount(nint self, uint* count) { *count = 1; return Ok; }
    [UnmanagedCallersOnly]
    static int GetProperty(nint self, uint index, nint* value)
    {
        *value = 0;
        if (index != 0) return InvalidArgument;
        // IPropertyValueStatics::CreateSingle is the ninth method after IInspectable's six.
        if (propertyValues == 0)
        {
            WindowsCreateString("Windows.Foundation.PropertyValue", 32, out var className);
            var guid = IPropertyValueStatics;
            var created = RoGetActivationFactory(className, &guid, out var factory);
            WindowsDeleteString(className);
            if (created != Ok) return created;
            if (Interlocked.CompareExchange(ref propertyValues, factory, 0) != 0) Marshal.Release(factory);
        }
        var createSingle = (delegate* unmanaged<nint, float, nint*, int>)(*(void***)propertyValues)[14];
        nint boxed;
        var result = createSingle(propertyValues, FromInterop(self)->Amount, &boxed);
        if (result != Ok) return result;
        var iid = IPropertyValue;
        result = Marshal.QueryInterface(boxed, in iid, out *value);
        Marshal.Release(boxed);
        return result;
    }
    [UnmanagedCallersOnly]
    static int GetSource(nint self, uint index, nint* source)
    {
        *source = 0;
        if (index != 0) return InvalidArgument;
        var value = FromInterop(self)->Source;
        Marshal.AddRef(value);
        *source = value;
        return Ok;
    }
    [UnmanagedCallersOnly] static int GetSourceCount(nint self, uint* count) { *count = 1; return Ok; }

    [DllImport("combase.dll", CharSet = CharSet.Unicode)] static extern int WindowsCreateString(string source, int length, out nint value);
    [DllImport("combase.dll")] static extern int WindowsDuplicateString(nint value, out nint copy);
    [DllImport("combase.dll")] static extern int WindowsDeleteString(nint value);
    [DllImport("combase.dll")] static extern int RoGetActivationFactory(nint className, Guid* iid, out nint factory);
}

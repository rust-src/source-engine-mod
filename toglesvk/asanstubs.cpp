// asanstubs.cpp - Stubs for display DB methods
// Vulkan backend version - no GL dependencies

typedef unsigned int uint;

// These are stubs for display enumeration that the GL backend provides.
// The Vulkan backend handles display enumeration through VK_KHR_surface
// extensions, so these are no-ops.

extern "C" {

// Provide minimal stubs that don't reference GL types
// The display DB is not used in the Vulkan code path
void VKDisplayDBStub_PopulateRenderers( void ) { }
void VKDisplayDBStub_PopulateFakeAdapters( uint realRendererIndex ) { }
void VKDisplayDBStub_Populate( void ) { }

}

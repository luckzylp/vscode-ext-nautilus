/**
 * @file vscode_nautilus_extension.c
 * @brief Nautilus file manager extension that adds "Open with VSCode" context menu item.
 *
 * This extension integrates Visual Studio Code into the GNOME Nautilus file manager
 * by registering a context menu provider. When users right-click on files or folders
 * in Nautilus, they can select "Open with VSCode" to launch VSCode with the selected
 * items opened in the editor.
 *
 * The extension uses the GObject type system and implements the NautilusMenuProvider
 * interface to provide menu items. It spawns the 'code' command-line tool with the
 * selected file paths as arguments.
 *
 * @author Kevin
 * @date 2026-05-26
 */

#include <gio/gio.h>
#include <glib-object.h>
#include <nautilus-extension.h>

/**
 * @brief Macro to obtain the GType for VSCodeExtension.
 *
 * This macro defines VSCODE_TYPE_EXTENSION as the result of calling
 * vscode_extension_get_type(), which registers the type in the GObject
 * type system if not already registered.
 */
#define VSCODE_TYPE_EXTENSION (vscode_extension_get_type())

/**
 * @brief VSCodeExtension private structure.
 *
 * This structure represents an instance of the VSCodeExtension class.
 * It extends GObject directly (no additional private data needed for
 * this simple extension). The GObject parent instance is included
 * via the GObject parent field.
 */
typedef struct {
  GObject parent; /**< Parent instance of GObject */
} VSCodeExtension;

/**
 * @brief VSCodeExtensionClass class structure.
 *
 * This structure represents the class portion of the VSCodeExtension type.
 * It extends GObjectClass and serves as the base class for all instances.
 * This extension does not override any class methods.
 */
typedef struct {
  GObjectClass parent_class; /**< Parent class of GObjectClass */
} VSCodeExtensionClass;

/**
 * @brief Interface initializer for NautilusMenuProvider.
 *
 * This function initializes the NautilusMenuProvider interface for the
 * VSCodeExtension type. It is called during type registration to set up
 * the interface implementation. Specifically, it assigns the get_file_items
 * interface method to our implementation.
 *
 * @param iface Pointer to the NautilusMenuProviderInterface to initialize
 */
static void
vscode_menu_provider_iface_init(NautilusMenuProviderInterface *iface);

/**
 * @brief Macro for defining the VSCodeExtension type with interface implementation.
 *
 * This macro uses G_DEFINE_DYNAMIC_TYPE_EXTENDED to:
 * 1. Define VSCodeExtension as a dynamic GObject type
 * 2. Register it under G_TYPE_OBJECT
 * 3. Implement NAUTILUS_TYPE_MENU_PROVIDER interface using
 *    vscode_menu_provider_iface_init
 *
 * The '0' flags parameter indicates no additional type flags.
 */
G_DEFINE_DYNAMIC_TYPE_EXTENDED(
    VSCodeExtension, vscode_extension, G_TYPE_OBJECT, 0,
    G_IMPLEMENT_INTERFACE_DYNAMIC(NAUTILUS_TYPE_MENU_PROVIDER,
                                  vscode_menu_provider_iface_init))

/**
 * @brief Callback to free the file list when signal handler completes.
 *
 * This function is registered as the destroy notify for the user_data passed
 * to g_signal_connect_data(). When the signal is disconnected or the menu item
 * is destroyed, this callback ensures proper cleanup of the file list by:
 * 1. Unreferencing each NautilusFileInfo object in the list
 * 2. Freeing the list structure itself
 *
 * @param data Pointer to the GList containing NautilusFileInfo objects
 * @param closure Unused GClosure parameter (required by signal API)
 */
static void free_file_list(gpointer data, GClosure *closure) {
  // Free the list and all contained GObject references
  g_list_free_full(data, g_object_unref);
}

/**
 * @brief Spawns Visual Studio Code with the selected files as arguments.
 *
 * This function is called when the user activates the "Open with VSCode" menu item.
 * It:
 * 1. Extracts file URIs from the NautilusFileInfo objects
 * 2. Converts URIs to local file paths
 * 3. Constructs an argument vector for g_spawn_async()
 * 4. Launches /usr/bin/code with all file paths as arguments
 *
 * The spawned process runs asynchronously with stdout/stderr redirected to /dev/null
 * to prevent any terminal output from interfering with the file manager.
 *
 * @param item The NautilusMenuItem that was activated (unused)
 * @param user_data Pointer to a GList of NautilusFileInfo objects representing selected files
 */
static void open_with_vscode(NautilusMenuItem *item, gpointer user_data) {
  // Extract the list of selected files from user_data
  GList *selected = user_data;

  // argv array for the command to spawn
  // GPtrArray automatically grows as we add elements
  GPtrArray *argv = g_ptr_array_new();

  // First argument: path to the VSCode executable
  // g_strdup() creates a copy that we own (will be freed by g_ptr_array)
  g_ptr_array_add(argv, g_strdup("/usr/bin/code"));

  // Iterate through all selected files and add their paths as arguments
  for (GList *l = selected; l; l = l->next) {
    // Get current NautilusFileInfo from the list
    NautilusFileInfo *info = NAUTILUS_FILE_INFO(l->data);

    // Obtain the URI for this file (e.g., "file:///home/user/file.txt")
    char *uri = nautilus_file_info_get_uri(info);

    if (!uri)
      continue; // Skip if no URI available

    // Convert the URI to a local file path
    // Returns allocated string that we own
    char *path = g_filename_from_uri(uri, NULL, NULL);

    // URI is no longer needed, free it immediately
    g_free(uri);

    if (!path)
      continue; // Skip if conversion failed (e.g., non-local URI)

    // Add the file path as an argument to the command
    // We own this pointer now - it will be freed when argv is freed
    g_ptr_array_add(argv, path);
  }

  // NULL terminator required by g_spawn_async() - marks end of arguments
  g_ptr_array_add(argv, NULL);

  // Error tracking for spawn operation
  GError *error = NULL;

  // Spawn the VSCode process asynchronously
  // G_SPAWN_SEARCH_PATH: search PATH for code executable
  // G_SPAWN_STDOUT_TO_DEV_NULL: suppress stdout output
  // G_SPAWN_STDERR_TO_DEV_NULL: suppress stderr output
  gboolean ok = g_spawn_async(NULL,                    // working directory (NULL = inherit)
                              (char **)argv->pdata,   // argument vector
                              NULL,                   // environment (NULL = inherit)
                              G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                                  G_SPAWN_STDERR_TO_DEV_NULL,  // spawn flags
                              NULL,                   // child setup function
                              NULL,                   // user data for child setup
                              NULL,                   // PID output (not needed)
                              &error);                // error output

  // Handle spawn failure gracefully
  if (!ok) {
    // Log warning with the error message from glib
    g_warning("VSCode launch failed: %s", error->message);
    // Free the error structure to prevent memory leak
    g_error_free(error);
  }

  // Clean up: free the argument array and all contained strings
  g_ptr_array_free(argv, TRUE);
}

/**
 * @brief Creates the "Open with VSCode" menu item for the context menu.
 *
 * This function is the main entry point for the NautilusMenuProvider interface.
 * It is called by Nautilus when building the context menu for selected files.
 *
 * The function creates a single menu item that, when activated, will open all
 * selected files in Visual Studio Code.
 *
 * @param provider Pointer to the VSCodeExtension instance (unused)
 * @param files GList of NautilusFileInfo objects representing the selected files
 * @return GList* A list containing the newly created menu item, or NULL if no files selected
 */
static GList *get_file_items(NautilusMenuProvider *provider, GList *files) {
  // Return NULL if no files are selected (empty selection)
  if (!files)
    return NULL;

  // Create a new menu item with unique ID, label, and tooltip
  // "vscode::open" - unique identifier for this extension's menu item
  // "Open with VSCode" - displayed text in the context menu
  // "Open selection in VSCode" - tooltip/description
  // NULL - icon (no custom icon)
  NautilusMenuItem *item = nautilus_menu_item_new(
      "vscode::open", "Open with VSCode", "Open selection in VSCode", NULL);

  // Create a deep copy of the file list since we need to own the data
  // g_object_ref increments reference count for each file
  GList *copy = g_list_copy_deep(files, (GCopyFunc)g_object_ref, NULL);

  // Connect the "activate" signal to our handler
  // When user clicks the menu item, open_with_vscode will be called with 'copy' as data
  // free_file_list will be called when the signal connection is destroyed
  g_signal_connect_data(item, "activate",           // signal name
                       G_CALLBACK(open_with_vscode), // handler function
                       copy,                         // user data passed to handler
                       free_file_list,               // destroy notify for user_data
                       0);                           // connection flags (default)

  // Append our menu item to a new list and return it
  // Nautilus owns the returned list
  return g_list_append(NULL, item);
}

/**
 * @brief Interface initializer implementation.
 *
 * Initializes the NautilusMenuProvider interface vtable for VSCodeExtension.
 * This function is called when the interface is being initialized for this type.
 * We simply assign our get_file_items implementation to the interface struct.
 *
 * @param iface Pointer to the NautilusMenuProviderInterface to initialize
 */
static void
vscode_menu_provider_iface_init(NautilusMenuProviderInterface *iface) {
  // Assign our menu provider implementation to the interface
  iface->get_file_items = get_file_items;
}

/**
 * @brief Instance initializer for VSCodeExtension.
 *
 * Called when a new VSCodeExtension instance is created. This extension
 * does not require any instance-specific initialization.
 *
 * @param self Pointer to the VSCodeExtension instance being initialized
 */
static void vscode_extension_init(VSCodeExtension *self) {}

/**
 * @brief Class initializer for VSCodeExtension.
 *
 * Called once when the VSCodeExtension class is first registered in the
 * type system. Used to set up class-wide data, install properties,
 * or override virtual methods. This extension does not require any
 * class-specific setup.
 *
 * @param klass Pointer to the VSCodeExtensionClass being initialized
 */
static void vscode_extension_class_init(VSCodeExtensionClass *klass) {}

/**
 * @brief Class finalizer for VSCodeExtension.
 *
 * Called when the VSCodeExtension class is being destroyed. Used to
 * clean up any class-wide resources allocated in class_init.
 * This extension does not allocate any class-wide resources.
 *
 * @param klass Pointer to the VSCodeExtensionClass being finalized
 */
static void vscode_extension_class_finalize(VSCodeExtensionClass *klass) {}

/**
 * @brief Module initialization function (Nautilus extension entry point).
 *
 * This function is called by Nautilus when the extension is first loaded.
 * It registers the VSCodeExtension type in the GObject type system,
 * making it available for use by Nautilus.
 *
 * This function is called exactly once when the extension shared library
 * is loaded into memory. The module parameter provides access to the
 * type module system for registering our extension type.
 *
 * @param module Pointer to the GTypeModule provided by Nautilus
 */
void nautilus_module_initialize(GTypeModule *module) {
  // Register the VSCodeExtension type with the Nautilus type system
  vscode_extension_register_type(module);
}

/**
 * @brief Returns the list of types provided by this extension.
 *
 * This function is called by Nautilus to query which GObject types
 * this extension provides. It returns a static array containing
 * the single VSCodeExtension type.
 *
 * @param[out] types Pointer to an array where the type will be stored
 * @param[out] num_types Pointer to integer where the count of types will be stored
 */
void nautilus_module_list_types(const GType **types, int *num_types) {
  // Static local array to hold the type (persists for lifetime of process)
  static GType t[1];

  // Store our single extension type in the array
  t[0] = VSCODE_TYPE_EXTENSION;

  // Set the output parameters
  *types = t;
  *num_types = 1;
}

/**
 * @brief Module shutdown function.
 *
 * Called when Nautilus is shutting down or when the extension is
 * being unloaded. Used to perform any necessary cleanup.
 *
 * This extension does not maintain any state that requires cleanup,
 * so this function is a no-op.
 */
void nautilus_module_shutdown(void) {
  // No cleanup required for this extension
}

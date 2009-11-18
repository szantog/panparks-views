<?php
// $Id$
/**
 * @file cache.inc
 *
 * Functions to load Views' data so that it knows what is available to
 * build queries from.
 */

/**
 * Load views files on behalf of modules.
 */
function _views_include_handlers() {
  views_module_include('views.inc');
}

/**
 * Load default views files on behalf of modules.
 */
function _views_include_default_views() {
  views_module_include('views_default.inc');
}

/**
 * Fetch Views' data from the cache
 */
function _views_fetch_data($table = NULL) {
  static $cache = NULL;
  if (!isset($cache)) {
    $start = microtime(TRUE);
    // NOTE: This happens whether we retrieve them from cache or otherwise.

    $data = views_cache_get('views_data', TRUE);
    if (!empty($data->data)) {
      $cache = $data->data;
    }

    if (empty($cache)) {
      views_include_handlers();
      $cache = module_invoke_all('views_data');
      foreach (module_implements('views_data_alter') as $module) {
        $function = $module . '_views_data_alter';
        $function($cache);
      }

      views_cache_set('views_data', $cache, TRUE);
    }

    debug('Views data build time: ' . (microtime(TRUE) - $start) * 1000 . ' ms');
  }

  if (!$table) {
    return $cache;
  }
  if (isset($cache[$table])) {
    return $cache[$table];
  }

  // Return an empty array if there is no match.
  return array();
}

/**
 * Fetch the plugin data from cache.
 */
function _views_fetch_plugin_data($type = NULL, $plugin = NULL) {
  static $cache = NULL;
  if (!isset($cache)) {
    $start = microtime(TRUE);
    views_include('plugins');
    views_include_handlers();

    $cache = views_discover_plugins();

    //debug('Views plugins build time: ' . (microtime(TRUE) - $start) * 1000 . ' ms');
  }

  if (!$type && !$plugin) {
    return $cache;
  }
  elseif (!$plugin) {
    // Not in the if above so the else below won't run
    if (isset($cache[$type])) {
      return $cache[$type];
    }
  }
  elseif (isset($cache[$type][$plugin])) {
    return $cache[$type][$plugin];
  }

  // Return an empty array if there is no match.
  return array();
}

/**
 * Scan all modules for default views and rebuild the default views cache.
 *
 * @return An associative array of all known default views.
 */
function _views_discover_default_views() {
  static $cache = NULL;

  if (!isset($cache)) {
    $data = views_cache_get('views_default_views', TRUE);

    if (isset($data->data) && is_array($data->data)) {
      $cache = $data->data;
    }
    else {
      views_include_default_views();
      $defaults = module_invoke_all('views_default_views');
      $cache = array();

      foreach ($defaults as $name => $view) {
        // Only views with a sufficiently high api version are eligible.
        if (isset($view->api_version) && $view->api_version >= 2) {
          // Do not cache dead handlers.
          $view->destroy();
          $cache[$name] = $view;
        }
      }

      // Allow modules to modify default views before they are cached.
      drupal_alter('views_default_views', $cache);

      views_cache_set('views_default_views', $cache, TRUE);
    }
  }

  return $cache;
}

/**
 * Set a cached item in the views cache.
 *
 * This is just a convenience wrapper around cache_set().
 *
 * @param $cid
 *   The cache ID of the data to store.
 * @param $data
 *   The data to store in the cache. Complex data types will be automatically serialized before insertion.
 *   Strings will be stored as plain text and not serialized.
 * @param $use_language
 *   If TRUE, the data will be cached specific to the currently active language.
 */
function views_cache_set($cid, $data, $use_language = FALSE) {
  global $language;

  if (variable_get('views_skip_cache', FALSE)) {
    return;
  }
  if ($use_language) {
    $cid .= ':' . $language->language;
  }

  cache_set($cid, $data, 'cache_views');
}

/**
 * Return data from the persistent views cache.
 *
 * This is just a convenience wrapper around cache_get().
 *
 * @param $cid
 *   The cache ID of the data to retrieve.
 * @param $use_language
 *   If TRUE, the data will be requested specific to the currently active language.
 */
function views_cache_get($cid, $use_language = FALSE) {
  global $language;

  if (variable_get('views_skip_cache', FALSE)) {
    return 0;
  }
  if ($use_language) {
    $cid .= ':' . $language->language;
  }

  return cache_get($cid, 'cache_views');
}

/**
 * @defgroup views_object_cache Non-volatile cache storage
 * @{
 * The non-volatile object cache is used to store an object while it is
 * being edited, so that we don't have to save until we're completely
 * done. The cache should be 'cleaned' on a regular basis, meaning to
 * remove old objects from the cache, but otherwise the data in this
 * cache must remain stable, as it includes unsaved changes.
 */

/**
 * Get an object from the non-volatile Views cache.
 *
 * This function caches in memory as well, so that multiple calls to this
 * will not result in multiple database reads.
 *
 * @param $obj
 *   A 32 character or less string to define what kind of object is being
 *   stored; primarily this is used to prevent collisions.
 * @param $name
 *   The name of the view (or other object) being stored.
 * @param $skip_cache
 *   Skip the memory cache, meaning this must be read from the db again.
 *
 * @return
 *   The data that was cached.
 */
function views_object_cache_get($obj, $name, $skip_cache = FALSE) {
  static $cache = array();
  $key = "$obj:$name";
  if ($skip_cache) {
    unset($cache[$key]);
  }

  if (!array_key_exists($key, $cache)) {
    $result = db_query("SELECT * FROM {views_object_cache} WHERE sid = :sid AND obj = :obj AND name = :name",
      array(
        ':sid' => session_id(),
        ':obj' => $obj,
        ':name' => $name
      ));
    foreach ($result as $data) {
      if ($data) {
        $cache[$key] = unserialize($data->data);
      }
    }
  }
  return isset($cache[$key]) ? $cache[$key] : NULL;
}

/**
 * Store an object in the non-volatile Views cache.
 *
 * @param $obj
 *   A 32 character or less string to define what kind of object is being
 *   stored; primarily this is used to prevent collisions.
 * @param $name
 *   The name of the view (or other object) being stored.
 * @param $cache
 *   The object to be cached. This will be serialized prior to writing.
 */
function views_object_cache_set($obj, $name, $cache) {
  views_object_cache_clear($obj, $name);
  db_insert('views_object_cache')
    ->fields(array(
      'sid' => session_id(),
      'obj' => $obj,
      'name' => $name,
      'data' => serialize($cache),
      'updated' => REQUEST_TIME,
    ))
    ->execute();
}

/**
 * Remove an object from the non-volatile Views cache
 *
 * @param $obj
 *   A 32 character or less string to define what kind of object is being
 *   stored; primarily this is used to prevent collisions.
 * @param $name
 *   The name of the view (or other object) being stored.
 */
function views_object_cache_clear($obj, $name) {
  db_delete('views_object_cache')
    ->condition('sid', session_id())
    ->condition('obj', $obj)
    ->condition('name', $name)
    ->execute();
}

/**
 * Remove all objects in the object cache that are older than the
 * specified age.
 *
 * @param $age
 *   The minimum age of objects to remove, in seconds. For example, 86400 is
 *   one day. Defaults to 7 days.
 */
function views_object_cache_clean($age = NULL) {
  if (empty($age)) {
    $age = 86400 * 7; // 7 days
  }
  db_delete('views_object_cache')
    ->condition('updated', REQUEST_TIME - $age, '<')
    ->execute();
}

/**
 * @}
 */
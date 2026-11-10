/* Shared transport for the OS portal and product application pages. */
(() => {
  function headers(extra = {}) {
    const result = { ...extra };
    const grant = sessionStorage.getItem('lunar_access_grant');
    const admin = sessionStorage.getItem('lunar_admin_grant') || sessionStorage.getItem('lunar_debug_token');
    if (grant) result['X-Lunar-Access-Grant'] = grant;
    if (admin) result['X-Lunar-Debug-Token'] = admin;
    return result;
  }
  function failure(status, body, fallback) {
    const code = body?.error?.code || fallback || 'device_request_failed';
    if (status === 401) {
      sessionStorage.removeItem('lunar_admin_grant');
      sessionStorage.removeItem('lunar_debug_token');
    }
    const error = new Error(body?.error?.message || code);
    error.code = code; error.status = status;
    return error;
  }
  async function request(url, options = {}) {
    const response = await fetch(url, { ...options, headers: headers(options.headers),
      cache: 'no-store', signal: options.signal ? AbortSignal.any([options.signal, AbortSignal.timeout(15000)]) : AbortSignal.timeout(15000) });
    let body;
    try { body = await response.json(); }
    catch { throw failure(response.status, null, 'device_response_invalid'); }
    if (!response.ok || body.ok === false) throw failure(response.status, body, response.statusText);
    return body;
  }
  function result(body) {
    if (!body.result || typeof body.result !== 'object') throw failure(0, null, 'device_result_missing');
    return body.result;
  }
  async function api(module, operation, payload, options = {}) {
    return result(await request(`/api/v1/modules/${module}/${operation}`, {
      ...(payload === undefined ? {} : { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) }), ...options }));
  }
  function upload(url, body, options = {}) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('PUT', url, true);
      for (const [key, value] of Object.entries(headers({ 'Content-Type': options.contentType || 'application/octet-stream' })))
        xhr.setRequestHeader(key, value);
      xhr.timeout = options.timeout || 300000;
      xhr.upload.onprogress = event => {
        if (event.lengthComputable) options.onProgress?.(event.loaded / event.total * 100);
      };
      xhr.onerror = () => reject(failure(0, null, 'upload_network_error'));
      xhr.ontimeout = () => reject(failure(0, null, 'upload_timeout'));
      xhr.onabort = () => reject(failure(0, null, 'upload_cancelled'));
      xhr.onload = () => {
        let value;
        try { value = JSON.parse(xhr.responseText); }
        catch { reject(failure(xhr.status, null, 'device_response_invalid')); return; }
        if (xhr.status < 200 || xhr.status >= 300 || value.ok === false) {
          reject(failure(xhr.status, value, xhr.statusText)); return;
        }
        try { resolve(result(value)); } catch (error) { reject(error); }
      };
      options.onStart?.(xhr);
      xhr.send(body);
    });
  }
  globalThis.LunarDeviceClient = Object.freeze({ request, api, upload });
})();

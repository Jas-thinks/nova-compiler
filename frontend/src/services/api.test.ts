import { api, ApiError, request } from './api'

const okJson = (body: unknown, status = 200) => Promise.resolve(new Response(JSON.stringify(body), { status, headers: { 'content-type': 'application/json' } }))

afterEach(() => vi.restoreAllMocks())

describe('api client', () => {
  it('posts JSON bodies to the right endpoint', async () => {
    const f = vi.spyOn(globalThis, 'fetch').mockImplementation(() => okJson({ success: true }))
    await api.compile('fn main() {}', ['dce'])
    const [url, init] = f.mock.calls[0]
    expect(url).toBe('/api/compile')
    expect(JSON.parse((init as RequestInit).body as string)).toEqual({ source: 'fn main() {}', passes: ['dce'], run: false })
  })
  it('turns validation errors into readable ApiError messages', async () => {
    vi.spyOn(globalThis, 'fetch').mockImplementation(() => okJson({ error: 'invalid_request', detail: [{ field: 'passes', message: 'unknown pass' }] }, 422))
    await expect(api.compile('x', null)).rejects.toMatchObject({ status: 422, message: expect.stringContaining('passes: unknown pass') })
  })
  it('reports an unreachable backend clearly', async () => {
    vi.spyOn(globalThis, 'fetch').mockRejectedValue(new TypeError('failed'))
    const err = (await request("/api/health").catch((e: unknown) => e)) as ApiError
    expect(err).toBeInstanceOf(ApiError)
    expect(err.message).toMatch(/Cannot reach the NOVA backend/)
  })
  it('handles non-JSON error bodies', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue(new Response('Bad Gateway', { status: 502 }))
    await expect(request('/api/x')).rejects.toMatchObject({ status: 502 })
  })
  it('unwraps the examples list', async () => {
    vi.spyOn(globalThis, 'fetch').mockImplementation(() => okJson({ examples: [{ id: 'a' }] }))
    expect(await api.examples()).toEqual([{ id: 'a' }])
  })
})

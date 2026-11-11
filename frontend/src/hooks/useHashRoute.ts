import { useEffect, useState } from 'react'

export function useHashRoute(defaultRoute: string): [string, (r: string) => void] {
  const read = () => window.location.hash.replace(/^#\/?/, '') || defaultRoute
  const [route, setRoute] = useState(read)
  useEffect(() => {
    const on = () => setRoute(read())
    window.addEventListener('hashchange', on)
    return () => window.removeEventListener('hashchange', on)
  }, []) // eslint-disable-line react-hooks/exhaustive-deps
  return [route, (r) => { window.location.hash = '/' + r }]
}

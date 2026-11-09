import React, { CSSProperties, useState } from 'react';
import generated from '../../assets/manifests/generated.json';
import type { ProductAsset } from '../../shared/types/content';
import './ProductMedia.css';

const ASSETS = (generated as unknown as { assets: Record<string, ProductAsset> }).assets;
const BASE = (import.meta as any).env?.BASE_URL ?? './';

export type ProductMediaVariant = 'capture' | 'studio' | 'detail' | 'object';

export interface ProductMediaProps {
  /** Asset id from assets/manifests/assets.json. Required: there is no implicit fallback. */
  asset: string;
  variant?: ProductMediaVariant;
  /** `sizes` attribute: how wide the image renders, so the browser picks the right file. */
  sizes: string;
  /** Only the hero image should be high priority. */
  priority?: boolean;
  alt?: string;
  /** Controlled perspective for the studio presentation (degrees). Keep small. */
  tilt?: { x?: number; y?: number };
  className?: string;
  style?: CSSProperties;
  imgStyle?: CSSProperties;
  /** Studio: draw the soft contact shadow beneath the object. */
  contactShadow?: boolean;
}

export function getAsset(id: string): ProductAsset | undefined {
  return ASSETS[id];
}

export function srcSet(asset: ProductAsset, kind: 'avif' | 'webp') {
  return asset.files[kind].map((f) => `${BASE}${f.file} ${f.width}w`).join(', ');
}

/**
 * Genuine product imagery only. If the asset is missing or fails to load, the component shows an
 * explicit error state naming the asset: it never substitutes a CSS recreation of the interface.
 */
export function ProductMedia({ asset: id, variant = 'capture', sizes, priority, alt, tilt, className = '', style, imgStyle, contactShadow = variant === 'studio' }: ProductMediaProps) {
  const asset = ASSETS[id];
  const [failed, setFailed] = useState(false);
  if (!asset || failed) {
    return (
      <div className={`pm pm--error ${className}`} style={{ aspectRatio: asset ? String(asset.aspect) : '4 / 3', ...style }} role="img" aria-label={`Product image unavailable (${id})`}>
        <span className="pm__error-text">Product media missing: <code>{id}</code></span>
      </div>
    );
  }
  const largest = asset.files.webp[asset.files.webp.length - 1];
  const transform = tilt ? `perspective(2400px) rotateX(${tilt.x ?? 0}deg) rotateY(${tilt.y ?? 0}deg)` : undefined;
  return (
    <figure className={`pm pm--${variant} ${className}`} style={style} data-asset={id}>
      <div className="pm__object" style={{ transform }}>
        <picture>
          <source type="image/avif" srcSet={srcSet(asset, 'avif')} sizes={sizes} />
          <img
            src={`${BASE}${largest.file}`}
            srcSet={srcSet(asset, 'webp')}
            sizes={sizes}
            width={asset.width}
            height={asset.height}
            alt={alt ?? asset.alt}
            loading={priority ? 'eager' : 'lazy'}
            decoding={priority ? 'sync' : 'async'}
            // @ts-expect-error: fetchpriority is valid HTML, typed in newer React
            fetchpriority={priority ? 'high' : 'auto'}
            onError={() => setFailed(true)}
            style={imgStyle}
            draggable={false}
          />
        </picture>
      </div>
      {contactShadow && <div className="pm__contact" aria-hidden="true" />}
    </figure>
  );
}

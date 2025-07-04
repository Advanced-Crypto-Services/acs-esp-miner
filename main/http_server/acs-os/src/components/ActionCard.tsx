import { Button } from "./Button";

interface ActionCardProps {
  title: string;
  description: string;
  buttonText?: string;
  onAction?: () => void;
  secondaryButtonText?: string;
  onSecondaryAction?: () => void;
  link?: string;
  children?: preact.ComponentChildren;
}

export function ActionCard({
  title,
  description,
  buttonText,
  onAction,
  secondaryButtonText,
  onSecondaryAction,
  link,
  children,
}: ActionCardProps) {
  return (
    <div className='bg-[var(--card-bg)]  shadow-md'>
      <h3 className='text-lg font-medium text-white mb-2'>{title}</h3>
      <p className='text-[#8B96A5] mb-4'>{description}</p>

      {children || (
        <div className='flex flex-wrap gap-3'>
          {buttonText && onAction && (
            <Button onClick={onAction} className='bg-blue-600 hover:bg-blue-700'>
              {buttonText}
            </Button>
          )}
          {link && (
            <Button
              as='a'
              href={link}
              download='esp-miner.bin'
              className='bg-blue-600 hover:bg-blue-700'
            >
              Download Firmware
            </Button>
          )}
          {secondaryButtonText && onSecondaryAction && (
            <Button variant='outline' onClick={onSecondaryAction}>
              {secondaryButtonText}
            </Button>
          )}
        </div>
      )}
    </div>
  );
}
